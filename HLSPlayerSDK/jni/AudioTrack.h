/*
 * AudioTrack.h
 *
 *  Created on: Jul 17, 2014
 *      Author: Mark
 */

#ifndef AUDIOTRACK_H_
#define AUDIOTRACK_H_

#include <jni.h>
#include <androidVideoShim.h>
#include <semaphore.h>
#include <RefCounted.h>

enum
{
	AUDIOTHREAD_WAIT,
	AUDIOTHREAD_CONTINUE,
	AUDIOTHREAD_FINISH
};

class AudioTrack : public RefCounted {
public:
	AudioTrack(JavaVM* jvm);
	~AudioTrack();

	bool Init();
	void Close();

	virtual void unload(); // from RefCounted

	bool Start();
	void Play();
	void Pause();
	void Flush();
	bool Stop(bool seeking = false);

	bool Set(android_video_shim::sp<android_video_shim::MediaSource> audioSource, bool alreadyStarted = false);
	bool Set23(android_video_shim::sp<android_video_shim::MediaSource23> audioSource, bool alreadyStarted = false);

	int Update();

	void shutdown();

	int64_t GetTimeStamp();
	void SetTimeStampOffset(double offsetSecs);

	int getBufferSize();

	bool UpdateFormatInfo();

private:
	jclass mCAudioTrack;
	jmethodID mAudioTrack;
	jmethodID mGetMinBufferSize;
	jmethodID mPlay;
	jmethodID mPause;
	jmethodID mStop;
	jmethodID mRelease;
	jmethodID mGetTimestamp;
	jmethodID mWrite;
	jmethodID mFlush;
	jmethodID mSetPositionNotificationPeriod;
	jmethodID mGetPlaybackHeadPosition;

	jobject mTrack;
	jarray buffer;

	JavaVM* mJvm;

	android_video_shim::sp<android_video_shim::MediaSource> mAudioSource;
	android_video_shim::sp<android_video_shim::MediaSource23> mAudioSource23;

	int mSampleRate;
	int mNumChannels;
	int mChannelMask;
	int mBufferSizeInBytes;

	int mPlayState;
	bool mWaiting;

	double mTimeStampOffset;

	long long samplesWritten;

	sem_t semPause;
    pthread_mutex_t updateMutex;

};

#endif /* AUDIOTRACK_H_ */
