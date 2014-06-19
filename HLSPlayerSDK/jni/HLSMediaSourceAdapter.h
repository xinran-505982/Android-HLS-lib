/*
 * HLSMediaSourceAdapter.h
 *
 *  Created on: May 2, 2014
 *      Author: Mark
 */

#ifndef HLSMEDIASOURCEADAPTER_H_
#define HLSMEDIASOURCEADAPTER_H_


#include <../android-source/frameworks/av/include/media/stagefright/MediaSource.h>
#include <../android-source/frameworks/av/include/media/stagefright/OMXCodec.h>
#include <../android-source/frameworks/av/include/media/stagefright/OMXClient.h>
#include <../android-source/frameworks/av/include/media/stagefright/MediaBuffer.h>
#include <../android-source/frameworks/av/include/media/stagefright/MetaData.h>

#include <list>

struct HLSMediaSourceAdapter : public android::MediaSource, public android::MediaBufferObserver
{
	HLSMediaSourceAdapter();

	void append(android::sp<android::MediaSource> source);

	virtual android::status_t start(android::MetaData * params = NULL);
    virtual android::status_t stop();

    virtual android::sp<android::MetaData> getFormat();

    virtual android::status_t read(
    		android::MediaBuffer **buffer, const ReadOptions *options = NULL);

    virtual android::status_t pause();

    // from MediaBufferObserver
    virtual void signalBufferReturned(android::MediaBuffer *buffer);

    void setIsAudio(bool val);

    size_t getWidth();
    size_t getHeight();
    size_t getCropWidth();
    size_t getCropHeight();

    int getSegmentCount();

    void setNeedMoreSegmentsCallback(void (*needMoreSegments)());

    void clear();




protected:
	virtual ~HLSMediaSourceAdapter();

private:
	void (*mNeedMoreSegments)();
	std::list<android::sp<android::MediaSource> > mSources;
	android::sp<android::MediaSource> mCurrentSource;

	android::sp<android::MetaData> mStartMetadata;
	bool mIsAudio;

	int32_t mWidth;
	int32_t mHeight;
	int32_t mCropWidth;
	int32_t mCropHeight;

	int64_t mFrameTimeDelta;
	int64_t mLastFrameTime;
	int64_t mTimestampOffset;
	int64_t mLastTimestamp;


};


#endif /* HLSMEDIASOURCEADAPTER_H_ */
