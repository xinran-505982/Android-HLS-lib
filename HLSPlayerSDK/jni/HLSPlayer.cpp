/*
 * HLSPlayer.cpp
 *
 *  Created on: May 5, 2014
 *      Author: Mark
 */

#include "HLSPlayer.h"
#include "debug.h"
#include "constants.h"
#include <android/log.h>
#include <android/native_window_jni.h>

#include "stlhelpers.h"
#include "HLSSegment.h"

#include "androidVideoShim_ColorConverter.h"


using namespace android_video_shim;


//////////
//
// Thread stuff
//
/////////

void* audio_thread_func(void* arg)
{
	AudioTrack* audioTrack = (AudioTrack*)arg;

	while (audioTrack->Update())
	{
		if(audioTrack->getBufferSize() > (44100/10))
		{
			//LOGI("Buffer full enough yielding");
			sched_yield();
		}
	}

	audioTrack->shutdown();
	LOGI("audio_thread_func ending");
	return NULL;
}


HLSPlayer::HLSPlayer(JavaVM* jvm) : mExtractorFlags(0),
mHeight(0), mWidth(0), mCropHeight(0), mCropWidth(0), mBitrate(0), mActiveAudioTrackIndex(-1),
mVideoBuffer(NULL), mWindow(NULL), mSurface(NULL), mRenderedFrameCount(0),
mDurationUs(0), mOffloadAudio(false), mStatus(STOPPED),
mAudioTrack(NULL), mVideoTrack(NULL), mJvm(jvm), mPlayerViewClass(NULL),
mNextSegmentMethodID(NULL), mSetVideoResolutionID(NULL), mEnableHWRendererModeID(NULL), 
mSegmentTimeOffset(0), mVideoFrameDelta(0), mLastVideoTimeUs(0),
mSegmentForTimeMethodID(NULL), mFrameCount(0), mDataSource(NULL), audioThread(0),
mScreenHeight(0), mScreenWidth(0), mJAudioTrack(NULL), mStartTimeMS(0), mVideoRenderer(NULL), mUseOMXRenderer(true)
{
	status_t status = mClient.connect();
	LOGI("OMXClient::Connect return %d", status);

	int err = pthread_mutex_init(&lock, NULL);
	LOGI(" HLSPlayer mutex err = %d", err);

}

HLSPlayer::~HLSPlayer()
{
}

void HLSPlayer::Close(JNIEnv* env)
{
	LOGI("Entered");
	Reset();
	if (mPlayerViewClass)
	{
		env->DeleteGlobalRef(mPlayerViewClass);
		mPlayerViewClass = NULL;
	}
	if (mWindow)
	{
		ANativeWindow_release(mWindow);
		mWindow = NULL;
	}
	if (mSurface)
	{
		(*env).DeleteGlobalRef(mSurface);
		mSurface = NULL;
	}
}

void HLSPlayer::Reset()
{
	LOGI("Entered");
	mStatus = STOPPED;
	LogState();

	mDataSource.clear();
	mAudioTrack.clear();
	mAudioTrack23.clear();
	mVideoTrack.clear();
	mVideoTrack23.clear();
	mExtractor.clear();
	mAudioSource.clear();
	mAudioSource23.clear();

	if (mJAudioTrack)
	{
		mJAudioTrack->Close(); // Stops the track internally, in case you were wondering.
		delete mJAudioTrack;
		mJAudioTrack = NULL;
	}
	LOGI("Killing the video buffer");
	if (mVideoBuffer)
	{
		mVideoBuffer->release();
		mVideoBuffer = NULL;
	}
	if (mVideoSource.get()) mVideoSource->stop();
	if (mVideoSource23.get()) mVideoSource23->stop();
	mVideoSource.clear();
	mVideoSource23.clear();

	LOGI("Killing the segments");
	stlwipe(mSegments);
	LOGI("Killing the audio & video tracks");

	mLastVideoTimeUs = 0;
	mSegmentTimeOffset = 0;
	mVideoFrameDelta = 0;
	mFrameCount = 0;
}

///
/// Set Surface. Takes a java surface object
///
void HLSPlayer::SetSurface(JNIEnv* env, jobject surface)
{
	LOGI("Entered");

	if (mWindow)
	{
		ANativeWindow_release(mWindow);
		mWindow = NULL;
	}
	if (mSurface)
	{
		(*env).DeleteGlobalRef(mSurface);
		mSurface = NULL;
	}

	if(surface)
	{
		// Note the surface.
		mSurface = (jobject)env->NewGlobalRef(surface);

		// Set up our rendering path.
		sp<MetaData> meta;
		if(AVSHIM_USE_NEWMEDIASOURCE)
			meta = mVideoSource->getFormat();
		else
			meta = mVideoSource23->getFormat();

		if(!meta.get())
		{
			LOGE("No format available from the video source.");
			return;
		}

		// HAX for hw renderer path
		const char *component = "";
		bool hasDecoderComponent = meta->findCString(kKeyDecoderComponent, &component);

		int colorFormat = -1;
		meta->findInt32(kKeyColorFormat, &colorFormat);

		if(mUseOMXRenderer)
		{
			// Set things up w/ OMX.
			LOGV("Trying OMXRenderer path!");

			LOGV("Getting IOMX");
			sp<IOMX> omx = mClient.interface();
			LOGV("   got %p", omx.get());

			int32_t decodedWidth, decodedHeight;
			meta->findInt32(kKeyWidth, &decodedWidth);
	    	meta->findInt32(kKeyHeight, &decodedHeight);
	    	int32_t vidWidth, vidHeight;
	    	mVideoTrack_md->findInt32(kKeyWidth, &vidWidth);
	        mVideoTrack_md->findInt32(kKeyHeight, &vidHeight);

			LOGI("Calling createRendererFromJavaSurface component='%s' %dx%d colf=%d", component, mWidth, mHeight, colorFormat);
			mOMXRenderer = omx.get()->createRendererFromJavaSurface(env, mSurface, 
				component, (OMX_COLOR_FORMATTYPE)colorFormat,
				decodedWidth, decodedHeight,
				vidWidth, vidHeight,
				0);
			LOGV("   o got %p", mOMXRenderer.get());

			if(!mOMXRenderer.get())
			{
				LOGE("OMXRenderer path failed, re-initializing with SW renderer path.");
				mUseOMXRenderer = false;
				NoteHWRendererMode(false, mWidth, mHeight, 4);
				return;
			}
		}

		if(!mOMXRenderer.get() && !mVideoRenderer)
		{
			LOGI("Initializing SW renderer path.");

			::ANativeWindow *window = ANativeWindow_fromSurface(env, mSurface);
			if(!window)
			{
				LOGE("Failed to get ANativeWindow from mSurface %p", mSurface);
				assert(window);
			}

			SetNativeWindow(window);
			int32_t res = ANativeWindow_setBuffersGeometry(mWindow, mWidth, mHeight, WINDOW_FORMAT_RGB_565);
		}
	}
	else
	{
		// Tear down renderers.
		mVideoRenderer = NULL;
		mSurface = NULL;
	}
}

void HLSPlayer::SetNativeWindow(::ANativeWindow* window)
{
	LOGI("window = %p", window);
	if (mWindow)
	{
		LOGI("::mWindow is already set to %p", window);
		// Umm - resetting?
		ANativeWindow_release(mWindow);
	}
	mWindow = window;
}


status_t HLSPlayer::FeedSegment(const char* path, int quality, double time )
{

	// Make a data source from the file
	LOGI("path = '%s'", path);
	if (mDataSource == NULL)
	{
		mDataSource = new HLSDataSource();
		if (mDataSource.get())
		{
			mDataSource->patchTable();
		}
		else
		{
			return NO_MEMORY;
		}
	}

	LOGI("mDataSource = %p", mDataSource.get());

	status_t err = mDataSource->append(path);
	if (err != OK)
	{
		LOGE("append Failed: %s", strerror(-err));
		LOGE("Provisionally continuing...");
		//return err;
	}

	// I don't know if we still need this - might need to pass in the URL instead of the datasource
	HLSSegment* s = new HLSSegment(quality, time);
	if (s)
	{
		mSegments.push_back(s);
		return OK;
	}
	return NO_MEMORY;
}

bool HLSPlayer::InitTracks()
{
	LOGI("Entered: mDataSource=%p", mDataSource.get());
	status_t err = mDataSource->initCheck();
	if (err != OK)
	{
		LOGE("DataSource is invalid: %s", strerror(-err));
		// Don't bail as the HTTP sources will return errors sometimes due to
		// internal race conditions.
		//return false;
	}


	mExtractor = MediaExtractor::Create(mDataSource, "video/mp2ts");
	if (mExtractor == NULL)
	{
		LOGE("Could not create MediaExtractor from DataSource @ %p", mDataSource.get());
		return false;
	}

//	if (mExtractor->getDrmFlag())
//	{
//		LOGERROR(METHOD, "This datasource has DRM - not implemented!!!");
//		return false;
//	}

	LOGI("Getting bit rate of streams.");
	int64_t totalBitRate = 0;
	for (size_t i = 0; i < mExtractor->countTracks(); ++i)
	{
		sp<MetaData> meta = mExtractor->getTrackMetaData(i); // this is likely to return an MPEG2TSSource

		int32_t bitrate = 0;
		if (!meta->findInt32(kKeyBitRate, &bitrate))
		{
			const char* mime = "[unknown]";
			meta->findCString(kKeyMIMEType, &mime);

			LOGI("Track #%d of type '%s' does not publish bitrate", i, mime );
			continue;
		}
		LOGI("bitrate for track %d = %d bits/sec", i , bitrate);
		totalBitRate += bitrate;
	}

	mBitrate = totalBitRate;
	LOGI("mBitrate = %lld bits/sec", mBitrate);

	bool haveAudio = false;
	bool haveVideo = false;

	for (size_t i = 0; i < mExtractor->countTracks(); ++i)
	{
		sp<MetaData> meta = mExtractor->getTrackMetaData(i);
		meta->dumpToLog();

		const char* cmime;
		if (meta->findCString(kKeyMIMEType, &cmime))
		{
			if (!haveVideo && !strncasecmp(cmime, "video/", 6))
			{
				if(AVSHIM_USE_NEWMEDIASOURCE)
					mVideoTrack = mExtractor->getTrack(i);
				else
					mVideoTrack23 = mExtractor->getTrack23(i);

				haveVideo = true;

				// Set the presentation/display size
				int32_t width, height;
				bool res = meta->findInt32(kKeyWidth, &width);
				if (res)
				{
					res = meta->findInt32(kKeyHeight, &height);
				}
				if (res)
				{
					mWidth = width;
					mHeight = height;
					NoteVideoDimensions();
					LOGI("Video Track Width = %d, Height = %d, %d", width, height, __LINE__);
				}

				mVideoTrack_md = meta;
			}
			else if (!haveAudio && !strncasecmp(cmime /*mime.string()*/, "audio/", 6))
			{
				if(AVSHIM_USE_NEWMEDIASOURCE)
					mAudioTrack = mExtractor->getTrack(i);
				else
					mAudioTrack23 = mExtractor->getTrack23(i);
				haveAudio = true;

				mActiveAudioTrackIndex = i;

				mAudioTrack_md = meta;
			}
//			else if (!strcasecmp(cmime /*mime.string()*/, MEDIA_MIMETYPE_TEXT_3GPP))
//			{
//				//addTextSource_l(i, mExtractor->getTrack(i));
//			}
		}
	}

	if (!haveAudio && !haveVideo)
	{
		return UNKNOWN_ERROR;
	}

	//mExtractorFlags = mExtractor->flags();

	return true;
}

bool HLSPlayer::CreateAudioPlayer()
{
	LOGI("Constructing JAudioTrack");
	mJAudioTrack = new AudioTrack(mJvm);
	if (!mJAudioTrack)
		return false;

	if (!mJAudioTrack->Init())
	{
		LOGE("JAudioTrack::Init() failed - quitting CreateAudioPlayer");
		mAudioTrack = NULL;
		return false;
	}

	if(mAudioSource.get())
		mJAudioTrack->Set(mAudioSource);
	else
		mJAudioTrack->Set23(mAudioSource23);

	return true;
}


bool HLSPlayer::InitSources()
{
	if (!InitTracks())
		return false;
	
	LOGI("Entered");
	
	if(AVSHIM_USE_NEWMEDIASOURCE)
	{
		if (mVideoTrack == NULL || mAudioTrack == NULL)
			return false;
	}
	else
	{
		if (mVideoTrack23 == NULL || mAudioTrack23 == NULL)
			return false;		
	}

	LOGV("Past initial sanity check...");

	// Video
	sp<IOMX> iomx = mClient.interface();

	sp<MetaData> vidFormat;
	if(mVideoTrack_md.get() != NULL)
	{
		LOGV("    o Path C");
		vidFormat = mVideoTrack_md;
	}
	else if(AVSHIM_USE_NEWMEDIASOURCE)
	{
		LOGV("    o Path A");
		vidFormat = mVideoTrack->getFormat();
	}
	else if (!AVSHIM_USE_NEWMEDIASOURCE)
	{
		LOGV("    o Path B");
		vidFormat = mVideoTrack23->getFormat();
	}
	else
	{
		LOGV("No path found!");
	}
	
	LOGV("vidFormat look up round 1 complete");

	if(vidFormat.get() == NULL)
	{
		LOGE("No format available from the video track.");
		return false;
	}
	
	LOGI("Creating hardware video decoder...");

	if(AVSHIM_USE_NEWMEDIASOURCE)
	{
		LOGV("   - taking 4.x path");
		LOGV("OMXCodec::Create - format=%p track=%p", vidFormat.get(), mVideoTrack.get());
		mVideoSource = OMXCodec::Create(iomx, vidFormat, false, mVideoTrack, NULL, 0);
		LOGV("   - got %p back", mVideoSource.get());
	}
	else
	{
		LOGV("   - taking 2.3 path");

		LOGV("OMXCodec::Create - format=%p track=%p", vidFormat.get(), mVideoTrack23.get());
		mVideoSource23 = OMXCodec::Create23(iomx, vidFormat, false, mVideoTrack23, NULL, 0);
		LOGV("   - got %p back", mVideoSource23.get());
	}
	
	LOGI("OMXCodec::Create() (video) returned %p", mVideoSource.get());

	sp<MetaData> meta;
	if(AVSHIM_USE_NEWMEDIASOURCE)
		meta = mVideoSource->getFormat();
	else
		meta = mVideoSource23->getFormat();

	if(!meta.get())
	{
		LOGE("No format available from the video source.");
		return false;
	}

	meta->findInt32(kKeyWidth, &mWidth);
	meta->findInt32(kKeyHeight, &mHeight);
	int32_t left, top;
	if(!meta->findRect(kKeyCropRect, &left, &top, &mCropWidth, &mCropHeight))
	{
		LOGW("Could not get crop rect, assuming full video size.");
		left = top = 0;
		mCropWidth = mWidth;
		mCropHeight = mHeight;
	}

	JNIEnv *env = NULL;
	mJvm->AttachCurrentThread(&env, NULL);
	LOGV(" env=%p", env);

	// HAX for hw renderer path
	const char *component = "";
	bool hasDecoderComponent = meta->findCString(kKeyDecoderComponent, &component);

	int colorFormat = -1;
	meta->findInt32(kKeyColorFormat, &colorFormat);

	if(AVSHIM_HAS_OMXRENDERERPATH && hasDecoderComponent 
		&& colorFormat != OMX_COLOR_Format16bitRGB565 // Don't need this if it's already in a good format.
		&& dlopen("libstagefrighthw.so", RTLD_LAZY)) // Skip it if the hw lib isn't present as that's where this class comes from.
	{
		// Set things up w/ OMX.
		LOGV("Trying OMXRenderer init path!");
		mUseOMXRenderer = true;
		NoteHWRendererMode(true, mWidth, mHeight, 4);
	}
	else
	{
		LOGV("Trying OMXRenderer init path!");
		mUseOMXRenderer = false;
		NoteHWRendererMode(false, mWidth, mHeight, 4);		
	}

	// We will get called back later to finish initialization of our renderers.

	// Audio
	if(AVSHIM_USE_NEWMEDIASOURCE)
		mOffloadAudio = canOffloadStream(mAudioTrack->getFormat(), (mVideoTrack != NULL), false /*streaming http */, AUDIO_STREAM_MUSIC);
	else
		mOffloadAudio = canOffloadStream(mAudioTrack23->getFormat(), (mVideoTrack23 != NULL), false /*streaming http */, AUDIO_STREAM_MUSIC);				

	LOGI("mOffloadAudio == %s", mOffloadAudio ? "true" : "false");

	sp<MetaData> audioFormat;
	if(AVSHIM_USE_NEWMEDIASOURCE)
		audioFormat = mAudioTrack->getFormat();
	else
		audioFormat = mAudioTrack23->getFormat();

	// Fall back to the MediaExtractor value for 3.x devices..
	if(audioFormat.get() == NULL)
		audioFormat = mAudioTrack_md;

	if(!audioFormat.get())
	{
		LOGE("No format available from the audio track.");
		return false;
	}

	audioFormat->dumpToLog();

	if(AVSHIM_USE_NEWMEDIASOURCE)
		mAudioSource = OMXCodec::Create(iomx, audioFormat, false, mAudioTrack, NULL, 0);
	else
		mAudioSource23 = OMXCodec::Create23(iomx, audioFormat, false, mAudioTrack23, NULL, 0);

	LOGI("OMXCodec::Create() (audio) returned %p %p", mAudioSource.get(), mAudioSource23.get());

	if (mOffloadAudio)
	{
		LOGI("Bypass OMX (offload) Line: %d", __LINE__);
		if(AVSHIM_USE_NEWMEDIASOURCE)
			mAudioSource = mAudioTrack;
		else
			mAudioSource23 = mAudioTrack23;
	}

	return true;
}

//
//  Play()
//
//		Tells the player to play the current stream. Requires that
//		segments have already been fed to the player.
//
bool HLSPlayer::Play()
{
	LOGI("Entered");
	
	if (!InitSources()) return false;

	/*if (!mWindow) 
	{
		LOGI("mWindow is NULL"); 
		return false; 
	}*/

	status_t err = OK;
	
	if(mVideoSource.get())
		err = mVideoSource->start();
	else
		err = mVideoSource23->start();

	if (err == OK)
	{
		if (err == OK)
		{
			if (CreateAudioPlayer())
			{
				LOGI("Starting audio playback");

#ifdef USE_AUDIO
				if (mJAudioTrack->Start())
				{
					if (pthread_create(&audioThread, NULL, audio_thread_func, (void*)mJAudioTrack  ) != 0)
						return false;
				}
#endif

				LOGI("   OK! err=%d", err);
				SetState(PLAYING);
				return true;
			}
			else
			{
				LOGI("Failed to create audio player : %d", __LINE__);
			}
		}
		else
		{
			LOGI("Audio Track failed to start: %s : %d", strerror(-err), __LINE__);
		}
	}
	else
	{
		LOGI("Video Track failed to start: %s : %d", strerror(-err), __LINE__);
	}
	return false;
}


int HLSPlayer::Update()
{
	LOGI("Entered");
	LogState();

	pthread_mutex_lock(&lock);

	if (GetState() == SEEKING)
	{
		int segCount = ((HLSDataSource*) mDataSource.get())->getPreloadedSegmentCount();
		LOGI("Segment Count %d", segCount);
		if (segCount < 1) // (current segment + 2)
		{
			pthread_mutex_unlock(&lock);
			return 0; // keep going!
		}
		SetState(PLAYING);
		if (mJAudioTrack)
			mJAudioTrack->Play();
	}
	
	if (GetState() != PLAYING)
	{
		LogState();
		pthread_mutex_unlock(&lock);
		return -1;
	}

	status_t audioPlayerStatus;
//	if (mAudioPlayer->reachedEOS(&audioPlayerStatus))
//	{
//		LOGI("Audio player is at EOS, stopping...");
//		mStatus = STOPPED;
//		return -1;
//	}
//	if (mJAudioTrack != NULL)
//		mJAudioTrack->Update();


	if (mDataSource != NULL)
	{
		int segCount = ((HLSDataSource*) mDataSource.get())->getPreloadedSegmentCount();
		LOGI("Segment Count %d", segCount);
		if (segCount < 3) // (current segment + 2)
			RequestNextSegment();
	}

	MediaSource::ReadOptions options;
	MediaSource23::ReadOptions options23;
	bool rval = -1;
	for (;;)
	{
		//LOGI("mVideoBuffer = %x", mVideoBuffer);
		RUNDEBUG(mVideoSource->getFormat()->dumpToLog());
		status_t err = OK;
		if (mVideoBuffer == NULL)
		{
			LOGI("Reading video buffer");
			if(mVideoSource.get())
				err = mVideoSource->read(&mVideoBuffer, &options);
			if(mVideoSource23.get())
				err = mVideoSource23->read(&mVideoBuffer, &options23);

			if (err == OK && mVideoBuffer->range_length() != 0) ++mFrameCount;
		}
		if (err != OK)
		{
			LOGI("err=%s,%x  Line: %d", strerror(-err), -err, __LINE__);
			switch (err)
			{
			case INFO_FORMAT_CHANGED:
			case INFO_DISCONTINUITY:
			case INFO_OUTPUT_BUFFERS_CHANGED:
				// If it doesn't have a valid buffer, maybe it's informational?
				if (mVideoBuffer == NULL) 
				{
					pthread_mutex_unlock(&lock);
					return 0;
				}
				break;
			case ERROR_END_OF_STREAM:
				//SetState(STOPPED);
				//PlayNextSegment();
				pthread_mutex_unlock(&lock);
				return -1;
				//LOGI("Saw end of stream but who really cares about that?");
				//return 0;
				break;
			default:
				SetState(STOPPED);
				// deal with any errors
				// in the sample code, they're sending the video event, anyway
				pthread_mutex_unlock(&lock);
				return -1;
			}
		}

		if (mVideoBuffer->range_length() != 0)
		{
			int64_t timeUs;
			bool rval = mVideoBuffer->meta_data()->findInt64(kKeyTime, &timeUs);
			if (!rval)
			{
				LOGI("Frame did not have time value: STOPPING");
				SetState(STOPPED);
				pthread_mutex_unlock(&lock);
				return -1;
			}

#ifdef USE_AUDIO
			int64_t audioTime = mJAudioTrack->GetTimeStamp();
#else
			// Set the audio time to the video time, which will keep the video running.
			// TODO: This should probably be set to system time with a delta, so that the video doesn't
			// run too fast.
			int64_t audioTime = timeUs;
#endif

			if (timeUs > mLastVideoTimeUs)
			{
				mVideoFrameDelta = timeUs - mLastVideoTimeUs;
			}
			else if (timeUs < mLastVideoTimeUs)
			{
				LOGE("timeUs = %lld | mLastVideoTimeUs = %lld :: Why did this happen? Were we seeking?", timeUs, mLastVideoTimeUs);
			}

			LOGI("audioTime = %lld | videoTime = %lld | diff = %lld | mVideoFrameDelta = %lld", audioTime, timeUs, audioTime - timeUs, mVideoFrameDelta);

			int64_t delta = audioTime - timeUs;

			mLastVideoTimeUs = timeUs;
			if (delta < -10000) // video is running ahead
			{
				LOGI("Video is running ahead - waiting til next time");
				sched_yield();
				break; // skip out - don't render it yet
			}
			else if (delta > 40000) // video is running behind
			{
				LOGI("Video is running behind - skipping frame");
				// Do we need to catch up?
				mVideoBuffer->release();
				mVideoBuffer = NULL;
				continue;
			}
			else
			{

				// We appear to have a valid buffer?! and we're in time!
				if (RenderBuffer(mVideoBuffer))
				{
					++mRenderedFrameCount;
					rval = mRenderedFrameCount;
					LOGI("mRenderedFrameCount = %d", mRenderedFrameCount);
				}
				else
				{
					LOGI("Render Buffer returned false: STOPPING");
					SetState(STOPPED);
					rval=-1;
				}
				mVideoBuffer->release();
				mVideoBuffer = NULL;
				break;

			}
		}

		LOGI("Found empty buffer (%d)", __LINE__);
		// Some decoders return spurious empty buffers we want to ignore
		mVideoBuffer->release();
		mVideoBuffer = NULL;

	}

	pthread_mutex_unlock(&lock);
    return rval; // keep going!
}

bool HLSPlayer::RenderBuffer(MediaBuffer* buffer)
{
	if(mVideoRenderer)
	{
		LOGV("Taking VideoRenderer path %p %d", buffer->data(), buffer->range_length());
		mVideoRenderer->render((const uint8_t *)buffer->data() + buffer->range_offset(), buffer->range_length(), NULL);
		buffer->release();
		sched_yield();
		return true;
	}

	if(mOMXRenderer.get())
	{
        int fmt = -1;
        mVideoSource23->getFormat()->findInt32(kKeyColorFormat, &fmt);

		LOGI("Cond1 for hw path colf=%d", fmt);

        void *id;
        if (buffer->meta_data()->findPointer(kKeyBufferID, &id)) 
        {
			LOGV2("Cond2 for hw path");
            mOMXRenderer->render(id);
			LOGV2("Cond3 for hw path");
			//sched_yield();
            return true;
        }
	}

	//LOGI("Entered");
	//LOGI("Rendering Buffer size=%d", buffer->size());
	if (!mWindow) { LOGI("mWindow is NULL"); return true; }
	if (!buffer) { LOGI("the MediaBuffer is NULL"); return true; }

	RUNDEBUG(buffer->meta_data()->dumpToLog());

	// Get the frame's width and height.
	int videoBufferWidth = 0, videoBufferHeight = 0, vbCropTop = 0, vbCropLeft = 0, vbCropBottom = 0, vbCropRight = 0;
	sp<MetaData> vidFormat;
	if(mVideoSource.get())
		vidFormat = mVideoSource->getFormat();
	if(mVideoSource23.get())
		vidFormat = mVideoSource23->getFormat();

	if(!vidFormat->findInt32(kKeyWidth, &videoBufferWidth) || !buffer->meta_data()->findInt32(kKeyHeight, &videoBufferHeight))
	{
		LOGV("Falling back to source dimensions.");
		if(!buffer->meta_data()->findInt32(kKeyWidth, &videoBufferWidth) || !vidFormat->findInt32(kKeyHeight, &videoBufferHeight))
		{
			// I hope we're right!
			LOGV("Setting best guess width/height %dx%d", mWidth, mHeight);
			videoBufferWidth = mWidth;
			videoBufferHeight = mHeight;			
		}
	}

	int stride = -1;
	if(!buffer->meta_data()->findInt32(kKeyStride, &stride))
	{
		LOGV("Trying source stride");
		if(!vidFormat->findInt32(kKeyStride, &stride))
		{
			LOGV("Got no source");
		}
	}

	int x = -1;
	buffer->meta_data()->findInt32(kKeyDisplayWidth, &x);
	LOGV("dwidth = %d", x);

	if(stride != -1)
	{
		LOGV("Got stride %d", stride);
	}

	if(!vidFormat->findRect(kKeyCropRect, &vbCropLeft, &vbCropTop, &vbCropRight, &vbCropBottom))
	{
		if(!buffer->meta_data()->findRect(kKeyCropRect, &vbCropLeft, &vbCropTop, &vbCropRight, &vbCropBottom))
		{
			vbCropTop = 0;
			vbCropLeft = 0;
			vbCropBottom = videoBufferHeight - 1;
			vbCropRight = videoBufferWidth - 1;
		}
	}
	LOGI("vbw=%d vbh=%d vbcl=%d vbct=%d vbcr=%d vbcb=%d", videoBufferWidth, videoBufferHeight, vbCropLeft, vbCropTop, vbCropRight, vbCropBottom);

	int colf = 0;
	bool res = vidFormat->findInt32(kKeyColorFormat, &colf);
	LOGV2("Found Frame Color Format: %s %d", res ? "true" : "false", colf);

	const char *omxCodecString = "";
	res = vidFormat->findCString(kKeyDecoderComponent, &omxCodecString);
	LOGV("Found Frame decoder component: %s %s", res ? "true" : "false", omxCodecString);

	ColorConverter_Local lcc((OMX_COLOR_FORMATTYPE)colf, OMX_COLOR_Format16bitRGB565);
	LOGV("Local ColorConversion from %x is valid: %s", colf, lcc.isValid() ? "true" : "false" );

	ColorConverter cc((OMX_COLOR_FORMATTYPE)colf, OMX_COLOR_Format16bitRGB565); // Should be getting these from the formats, probably
	LOGV("System ColorConversion from %x is valid: %s", colf, cc.isValid() ? "true" : "false" );

	bool useLocalCC = lcc.isValid();	
	if (!useLocalCC && !lcc.isValid())
	{
		LOGE("No valid color conversion found for %d", colf);
		return false;
	}

	int64_t timeUs;
    if (buffer->meta_data()->findInt64(kKeyTime, &timeUs))
    {
		ANativeWindow_Buffer windowBuffer;
		if (ANativeWindow_lock(mWindow, &windowBuffer, NULL) == 0)
		{
			LOGV("buffer locked (%d x %d stride=%d, format=%d)", windowBuffer.width, windowBuffer.height, windowBuffer.stride, windowBuffer.format);

			int32_t targetWidth = windowBuffer.stride;
			int32_t targetHeight = windowBuffer.height;

			// Clear to black.
			unsigned short *pixels = (unsigned short *)windowBuffer.bits;
			memset(pixels, 0, windowBuffer.stride * windowBuffer.height * 2);

			unsigned char *videoBits = (unsigned char*)buffer->data() + buffer->range_offset();

			LOGV("mWidth=%d | mHeight=%d | mCropWidth=%d | mCropHeight=%d | buffer.width=%d | buffer.height=%d videoBits=%p",
							mWidth, mHeight, mCropWidth, mCropHeight, windowBuffer.width, windowBuffer.height, videoBits);

			int32_t offsetx = (windowBuffer.width - videoBufferWidth) / 2;
			if (offsetx & 1 == 1) ++offsetx;
			int32_t offsety = (windowBuffer.height - videoBufferHeight) / 2;

			LOGV("converting source coords, %d, %d, %d, %d, %d, %d", videoBufferWidth, videoBufferHeight, vbCropLeft, vbCropTop, vbCropRight, vbCropBottom);
			LOGV("converting target coords, %d, %d, %d, %d, %d, %d", targetWidth, targetHeight, vbCropLeft + offsetx, vbCropTop + offsety, vbCropRight + offsetx, vbCropBottom + offsety);
			status_t ccres = OK;
			if (useLocalCC)
				ccres = lcc.convert(videoBits, videoBufferWidth, videoBufferHeight, vbCropLeft, vbCropTop, vbCropRight, vbCropBottom,
						windowBuffer.bits, targetWidth, targetHeight, vbCropLeft + offsetx, vbCropTop + offsety, vbCropRight + offsetx, vbCropBottom + offsety);
			else
				cc.convert(videoBits, videoBufferWidth, videoBufferHeight, vbCropLeft, vbCropTop, vbCropRight, vbCropBottom,
						windowBuffer.bits, targetWidth, targetHeight, vbCropLeft + offsetx, vbCropTop + offsety, vbCropRight + offsetx, vbCropBottom + offsety);

			if (ccres != OK) LOGE("ColorConversion error: %s (%d)", strerror(-ccres), -ccres);

			ANativeWindow_unlockAndPost(mWindow);

			sched_yield();
		}

		return true;
    }
    return false;

}

void HLSPlayer::SetState(int status)
{
	if (mStatus != status)
	{
		mStatus = status;
		LOGI("Status Changed");
		LogState();
	}
}

void HLSPlayer::LogState()
{
	switch (mStatus)
	{
	case STOPPED:
		LOGI("State = STOPPED");
		break;
	case PAUSED:
		LOGI("State = PAUSED");
		break;
	case PLAYING:
		LOGI("State = PLAYING");
		break;
	case SEEKING:
		LOGI("State = SEEKING");
		break;
	}
}

void HLSPlayer::RequestNextSegment()
{
	LOGI("Requesting new segment");
	JNIEnv* env = NULL;
	mJvm->AttachCurrentThread(&env, NULL);

	if (mPlayerViewClass == NULL)
	{
		jclass c = env->FindClass("com/kaltura/hlsplayersdk/PlayerViewController");
		if ( env->ExceptionCheck() || c == NULL) {
			LOGI("Could not find class com/kaltura/hlsplayersdk/PlayerViewController" );
			mPlayerViewClass = NULL;
			return;
		}

		mPlayerViewClass = (jclass)env->NewGlobalRef((jobject)c);

	}

	if (mNextSegmentMethodID == NULL)
	{
		mNextSegmentMethodID = env->GetStaticMethodID(mPlayerViewClass, "requestNextSegment", "()V" );
		if (env->ExceptionCheck())
		{
			mNextSegmentMethodID = NULL;
			LOGI("Could not find method com/kaltura/hlsplayersdk/PlayerViewController.requestNextSegment()" );
			return;
		}
	}

	env->CallStaticVoidMethod(mPlayerViewClass, mNextSegmentMethodID);
	if (env->ExceptionCheck())
	{
		LOGI("Call to method  com/kaltura/hlsplayersdk/PlayerViewController.requestNextSegment() FAILED" );
	}
}

double HLSPlayer::RequestSegmentForTime(double time)
{
	LOGI("Requesting segment for time %lf", time);
	JNIEnv* env = NULL;
	mJvm->AttachCurrentThread(&env, NULL);

	if (mPlayerViewClass == NULL)
	{
		jclass c = env->FindClass("com/kaltura/hlsplayersdk/PlayerViewController");
		if ( env->ExceptionCheck() || c == NULL) {
			LOGI("Could not find class com/kaltura/hlsplayersdk/PlayerViewController" );
			mPlayerViewClass = NULL;
			return 0;
		}

		mPlayerViewClass = (jclass)env->NewGlobalRef((jobject)c);

	}

	if (mSegmentForTimeMethodID == NULL)
	{
		mSegmentForTimeMethodID = env->GetStaticMethodID(mPlayerViewClass, "requestSegmentForTime", "(D)D" );
		if (env->ExceptionCheck())
		{
			mSegmentForTimeMethodID = NULL;
			LOGI("Could not find method com/kaltura/hlsplayersdk/PlayerViewController.requestSegmentForTime()" );
			return 0;
		}
	}

	jdouble segTime = env->CallStaticDoubleMethod(mPlayerViewClass, mSegmentForTimeMethodID, time);
	if (env->ExceptionCheck())
	{
		LOGI("Call to method  com/kaltura/hlsplayersdk/PlayerViewController.requestSegmentForTime() FAILED" );
	}
	return segTime;
}

void HLSPlayer::NoteVideoDimensions()
{
	LOGI("Noting video dimensions.");
	JNIEnv* env = NULL;
	mJvm->AttachCurrentThread(&env, NULL);

	if (mPlayerViewClass == NULL)
	{
		jclass c = env->FindClass("com/kaltura/hlsplayersdk/PlayerViewController");
		if ( env->ExceptionCheck() || c == NULL) {
			LOGI("Could not find class com/kaltura/hlsplayersdk/PlayerViewController" );
			mPlayerViewClass = NULL;
			return;
		}

		mPlayerViewClass = (jclass)env->NewGlobalRef((jobject)c);
	}

	if (mSetVideoResolutionID == NULL)
	{
		mSetVideoResolutionID = env->GetStaticMethodID(mPlayerViewClass, "setVideoResolution", "(II)V" );
		if (env->ExceptionCheck())
		{
			mSetVideoResolutionID = NULL;
			LOGI("Could not find method com/kaltura/hlsplayersdk/PlayerViewController.setVideoResolution()" );
			return;
		}
	}

	env->CallStaticVoidMethod(mPlayerViewClass, mSetVideoResolutionID, mWidth, mHeight);
	if (env->ExceptionCheck())
	{
		LOGI("Call to method  com/kaltura/hlsplayersdk/PlayerViewController.setVideoResolution() FAILED" );
	}	
}


void HLSPlayer::NoteHWRendererMode(bool enabled, int w, int h, int colf)
{
	LOGI("Noting video dimensions.");
	JNIEnv* env = NULL;
	mJvm->AttachCurrentThread(&env, NULL);

	if (mPlayerViewClass == NULL)
	{
		jclass c = env->FindClass("com/kaltura/hlsplayersdk/PlayerViewController");
		if ( env->ExceptionCheck() || c == NULL) {
			LOGI("Could not find class com/kaltura/hlsplayersdk/PlayerViewController" );
			mPlayerViewClass = NULL;
			return;
		}

		mPlayerViewClass = (jclass)env->NewGlobalRef((jobject)c);
	}

	if (mEnableHWRendererModeID == NULL)
	{
		mEnableHWRendererModeID = env->GetStaticMethodID(mPlayerViewClass, "enableHWRendererMode", "(ZIII)V" );
		if (env->ExceptionCheck())
		{
			mEnableHWRendererModeID = NULL;
			LOGI("Could not find method com/kaltura/hlsplayersdk/PlayerViewController.enableHWRendererMode()" );
			return;
		}
	}

	env->CallStaticVoidMethod(mPlayerViewClass, mEnableHWRendererModeID, enabled, w, h, colf);
	if (env->ExceptionCheck())
	{
		env->ExceptionDescribe();
		LOGI("Call to method com/kaltura/hlsplayersdk/PlayerView.enableHWRendererMode() FAILED" );
	}	
}

int HLSPlayer::GetState()
{
	return mStatus;
}

void HLSPlayer::TogglePause()
{
	LogState();
	if (GetState() == PAUSED)
	{
		SetState(PLAYING);
		mJAudioTrack->Play();
	}
	else if (GetState() == PLAYING)
	{
		SetState(PAUSED);
		mJAudioTrack->Pause();
	}
}

void HLSPlayer::Stop()
{
	LOGI("STOPPING!");
	LogState();
	if (GetState() == PLAYING)
	{
		SetState(STOPPED);
		mJAudioTrack->Stop();
	}
}

int32_t HLSPlayer::GetCurrentTimeMS()
{
	if (mJAudioTrack != NULL)
	{
		return (mJAudioTrack->GetTimeStamp() / 1000) + mStartTimeMS;
	}
	return 0;
}


void HLSPlayer::StopEverything()
{
	pthread_mutex_lock(&lock);

	mJAudioTrack->Stop(true); // Passing true means we're seeking.

	mAudioTrack.clear();
	mAudioTrack23.clear();
	mVideoTrack.clear();
	mVideoTrack23.clear();
	mExtractor.clear();
	mAudioSource.clear();
	mAudioSource23.clear();

	LOGI("Killing the video buffer");
	if (mVideoBuffer)
	{
		mVideoBuffer->release();
		mVideoBuffer = NULL;
	}
	if (mVideoSource != NULL) mVideoSource->stop();
	mVideoSource.clear();
	if (mVideoSource23 != NULL) mVideoSource23->stop();
	mVideoSource23.clear();

	mLastVideoTimeUs = 0;
	mSegmentTimeOffset = 0;
	mVideoFrameDelta = 0;
	mFrameCount = 0;

	pthread_mutex_unlock(&lock);
}


void HLSPlayer::Seek(double time)
{
	if (time < 0) time = 0;

	SetState(SEEKING);

	StopEverything();
	mDataSource->clearSources();

	double segTime = RequestSegmentForTime(time);

	mStartTimeMS = (segTime * 1000);

	LOGI("Segment Start Time = %f", segTime);

	int segCount = ((HLSDataSource*) mDataSource.get())->getPreloadedSegmentCount();
	if (!InitSources())
	{
		LOGE("InitSources failed!");
		return;
	}

	status_t err;
	if(mVideoSource.get())
		err = mVideoSource->start();
	else
		err = mVideoSource23->start();

	if (err == OK)
	{
		if(mAudioSource.get())
			mJAudioTrack->Set(mAudioSource);
		else
			mJAudioTrack->Set23(mAudioSource23);
	}
	else
	{
		LOGI("Video Track failed to start: %s : %d", strerror(-err), __LINE__);
	}
	LOGI("Segment Count %d", segCount);
	SetState(PLAYING);
	if (mJAudioTrack)
	{
		mJAudioTrack->Play();
	}

}
