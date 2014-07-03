/*
 * HLSSegment.h
 *
 *  Created on: Apr 29, 2014
 *      Author: Mark
 */

#ifndef HLSSEGMENT_H_
#define HLSSEGMENT_H_

#include <../android-source/frameworks/av/include/media/stagefright/MediaBuffer.h>
#include <../android-source/frameworks/av/include/media/stagefright/MediaSource.h>
#include <../android-source/frameworks/av/include/media/stagefright/MediaExtractor.h>
#include <../android-source/frameworks/av/include/media/stagefright/DataSource.h>
#include <../android-source/frameworks/av/include/media/stagefright/MetaData.h>


class HLSSegment
{
public:
	HLSSegment(int32_t quality, double time);
	~HLSSegment();

	bool SetDataSource(android::sp<android::DataSource> dataSource);

	android::sp<android::MediaSource> GetAudioTrack();
	android::sp<android::MediaSource> GetVideoTrack();

	int32_t GetWidth();
	int32_t GetHeight();
	int32_t GetQuality();
	double GetStartTime();

	int64_t GetBitrate();



private:

	bool InitAudioDecoder();
	void SetVideoTrack(android::sp<android::MediaSource> source);
	void SetAudioTrack(android::sp<android::MediaSource> source);

	int64_t mBitrate;
	uint32_t mExtractorFlags;
	int32_t mWidth;
	int32_t mHeight;
	int32_t mActiveAudioTrackIndex;
	int32_t mQuality;
	double mStartTime;
	android::sp<android::DataSource> mFileSource;
	android::sp<android::MediaExtractor> mExtractor;
	android::sp<android::MediaSource> mAudioTrack;
	android::sp<android::MediaSource> mVideoTrack;

};



#endif /* HLSSEGMENT_H_ */