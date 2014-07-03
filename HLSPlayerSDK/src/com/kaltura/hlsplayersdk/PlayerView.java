package com.kaltura.hlsplayersdk;

import java.net.MalformedURLException;
import java.net.URL;
import java.util.Timer;

import com.kaltura.hlsplayersdk.events.OnPlayerStateChangeListener;
import com.kaltura.hlsplayersdk.events.OnPlayheadUpdateListener;
import com.kaltura.hlsplayersdk.events.OnProgressListener;
import com.kaltura.hlsplayersdk.manifest.ManifestParser;
import com.kaltura.hlsplayersdk.manifest.ManifestSegment;
import com.kaltura.hlsplayersdk.manifest.events.OnParseCompleteListener;

import android.content.Context;
import android.media.MediaPlayer.OnErrorListener;
import android.media.MediaPlayer.OnPreparedListener;
import android.os.Handler;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceView;
import android.widget.MediaController.MediaPlayerControl;
import android.widget.VideoView;

public class PlayerView extends SurfaceView implements VideoPlayerInterface, MediaPlayerControl, OnParseCompleteListener, URLLoader.DownloadEventListener 
{
	
	private final int STATE_STOPPED = 0;
	private final int STATE_PAUSED = 1;
	private final int STATE_PLAYING = 2;
	private final int STATE_SEEKING = 3;
	
	// Native Methods
	private native void InitNativeDecoder();
	private native void CloseNativeDecoder();
	private native void ResetPlayer();
	private native void PlayFile();
	private native void StopPlayer();
	private native void TogglePause();
	private native void SetSurface(Surface surface);
	private native void NextFrame();
	private native void FeedSegment(String url, int quality, double startTime);
	private native void SeekTo(double time); // seconds, not miliseconds - I'll change this later if it
	private native int GetState();
	//private native double CurTim
	
	private static PlayerView currentPlayerView = null;
	
	public static void requestNextSegment()
	{
		if (currentPlayerView != null)
		{
			ManifestSegment seg = currentPlayerView.getStreamHandler().getNextFile(0);
			if (seg != null)
			{
				currentPlayerView.FeedSegment(seg.uri, 0, seg.startTime);
			}
		}
	}
	
	public static void requestSegmentForTime(double time)
	{
		if (currentPlayerView != null)
		{
			ManifestSegment seg = currentPlayerView.getStreamHandler().getFileForTime(time, 0);
			currentPlayerView.FeedSegment(seg.uri, 0, seg.startTime);
		}
	}
	
	private int frameDelay = 10;

	// This is our root manifest
	private ManifestParser mManifest = null;
	
	private URLLoader manifestLoader;
	private StreamHandler mStreamHandler = null;
	
	
	private Handler handler = new Handler();
	private Runnable runnable = new Runnable()
	{
		public void run()
		{
			if (GetState() == STATE_PLAYING)
			{
				//Log.i("Runnable.run", "Running!");
				NextFrame();
				postDelayed(runnable, frameDelay);
			}
		}
	};
	
	// setVideoUrl()
	// Sets the video URL and initiates the download of the manifest
	@Override
	public void setVideoUrl(String url) {
		Log.i("PlayerView.setVideoUrl", url);
		//layoutParams lp = this.getLayoutParams();
		stop(); // We don't call StopPlayer here because we want to stop everything, including the update pump
		ResetPlayer();
		manifestLoader = new URLLoader(this, null);
		manifestLoader.get(url);
	}
	
	
	// Called when the manifest parser is complete. Once this is done, play can actually start
	@Override
	public void onParserComplete(ManifestParser parser)
	{
		Log.i("PlayerView.onParserComplete", "Entered");
		mStreamHandler = new StreamHandler(parser);
		//mStreamHandler.initialize(parser);
		ManifestSegment seg = getStreamHandler().getFileForTime(0, 0);
		currentPlayerView.FeedSegment(seg.uri, 0, seg.startTime);
		seg = getStreamHandler().getNextFile(0);
		currentPlayerView.FeedSegment(seg.uri, 0, seg.startTime);
		play();
		//parser.dumpToLog();
	}
	
	@Override
	public void onDownloadComplete(URLLoader loader, String response)
	{
		mManifest = new ManifestParser();
		mManifest.setOnParseCompleteListener(this);
		mManifest.parse(response, loader.getRequestURI().toString());
	}
	
	@Override
	public void onDownloadFailed(URLLoader loader, String response)
	{
		
	}
	
	// Class Methods
	public PlayerView(Context context)
	{
		
		super(context);
		try
		{
			System.loadLibrary("HLSPlayerSDK");
			InitNativeDecoder();
		}
		catch (Exception e)
		{
			
		}
		currentPlayerView = this;
	}
	
	@Override
	public void close()
	{
		CloseNativeDecoder();
	}
	
	
	@Override
	public String getVideoUrl()
	{
		return "Not Implemented";
	}
	
	public int getDuration()
	{
		return 10;
	}
	
	
	public int getCurrentPosition()
	{
		return 0;
	}
	
	public boolean isPlaying()
	{
		return GetState() == STATE_PLAYING;
	}
	
	@Override
	public boolean getIsPlaying()
	{
		return isPlaying();
	}
	
	@Override
	public void play()
	{
		SetSurface(getHolder().getSurface());
		PlayFile();
		this.postDelayed(runnable, frameDelay);
//		if (!this.isPlaying())
//		{
//			super.start();
//		}
	}
	
	public boolean canPause()
	{
		return true;
	}
	
	public boolean canSeekBackward()
	{
		return true;
	}
	
	public boolean canSeekForward()
	{
		return true;		
	}
	
	public int getAudioSessionId()
	{
		return 1;
	}
	
	public int getBufferPercentage()
	{
		return 0;
	}
	
	public void seekTo(int pos)
	{
		
	}
	
	public void start()
	{
		
	}
	
	
	public void pause()
	{
		TogglePause();
	}
	
	@Override
	public void stop()
	{
		StopPlayer();
		try {
			Thread.sleep(frameDelay * 2);
		} catch (InterruptedException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		//super.stopPlayback();
	}
	
	@Override
	public void seek(int msec)
	{
		//super.seekTo(msec);
	}

	public StreamHandler getStreamHandler()
	{
		return mStreamHandler;
	}

	

	@Override
	public void registerPlayerStateChange(OnPlayerStateChangeListener listener) {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void registerReadyToPlay(OnPreparedListener listener) {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void registerError(OnErrorListener listener) {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void registerPlayheadUpdate(OnPlayheadUpdateListener listener) {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void registerProgressUpdate(OnProgressListener listener) {
		// TODO Auto-generated method stub
		
	}
	
	
	

}