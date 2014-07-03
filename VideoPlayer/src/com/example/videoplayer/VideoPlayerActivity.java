package com.example.videoplayer;

import android.support.v7.app.ActionBarActivity;
import android.support.v7.app.ActionBar;
import android.support.v4.app.Fragment;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.VideoView;
import android.os.Build;

import com.kaltura.hlsplayersdk.PlayerViewController;

public class VideoPlayerActivity extends ActionBarActivity {

	PlayerViewController playerView = null;
	final Context context = this;
	String lastUrl = "";
	
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_video_player);
        
      
        //Uri uri=Uri.parse("http://www.ebookfrenzy.com/android_book/movie.mp4");
        
        
        try
        {
        	
        	playerView = (PlayerViewController)findViewById(R.id.custom_player);
        	playerView.addComponents("", this);

        }
        catch (Exception e)
        {
        	Log.e("KalturaTestApp", e.getMessage());
        }
        

    }
    
    @Override
    public void onStop()
    {
    	if (playerView != null) playerView.close();
    	super.onStop();
    }


    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.video_player, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();
        if (id == R.id.action_settings) {
        	lastUrl = "http://www.kaltura.com/p/0/playManifest/entryId/1_0i2t7w0i/format/applehttp";
        	playerView.setVideoUrl(lastUrl);
        	//playerView.setVideoUrl("http://kalturavod-vh.akamaihd.net/i/p/1281471/sp/128147100/serveFlavor/entryId/1_0i2t7w0i/v/1/flavorId/1_rncam6d3/index_0_av.m3u8");
        	//playerView.play();
            return true;
        }
        else if (id == R.id.abc_dvr_item)
        {
        	lastUrl = "http://abclive.abcnews.com/i/abc_live4@136330/index_500_av-p.m3u8?sd=10&rebase=on";
        	playerView.setVideoUrl(lastUrl);
        	//playerView.setVideoUrl("http://abclive.abcnews.com/i/abc_live4@136330/master.m3u8");
        	return true;
        }
        else if (id == R.id.seekFwd)
        {
        	playerView.seek(15000);
        	return true;
        }
        else if (id == R.id.seekBwd)
        {
        	playerView.seek(-15000);
        	return true;
        }
        else if (id == R.id.testUrl)
        {
        	//playerView.setVideoUrl("https://dl.dropboxusercontent.com/u/41430608/TestStream/index_500_av-p.m3u8");
        	lastUrl = "http://www.djing.com/tv/live.m3u8";
        	playerView.setVideoUrl(lastUrl);
        	return true;
        }
        else if (id == R.id.play_pause)
        {
        	playerView.pause();
        }
        else if (id == R.id.openUrl)
        {
        	// start another popup to enter the URL, somehow
        	LayoutInflater layoutInflater = LayoutInflater.from(context);
        	
        	View urlInputView = layoutInflater.inflate(R.layout.url_input , null);
        	
        	AlertDialog.Builder alertDialogBuilder = new AlertDialog.Builder(context);
        	
        	// set url_input.xml to be the layout file of the alertDialog builder
        	alertDialogBuilder.setView(urlInputView );
        	
        	final EditText input = (EditText)urlInputView.findViewById(R.id.userInput);
        	
        	// set up a dialog window
        	alertDialogBuilder
        		.setCancelable(false)
        		.setPositiveButton("OK", new DialogInterface.OnClickListener() {
					public void onClick(DialogInterface dialog, int id) {
						// get user input and set it to result
						lastUrl = input.getText().toString();
						playerView.setVideoUrl(lastUrl);
					}
				})
				.setNegativeButton("Cancel",
						new DialogInterface.OnClickListener() {
							public void onClick(DialogInterface dialog,	int id) {
								dialog.cancel();
							}
						});
        	
			// create an alert dialog
			AlertDialog alertD = alertDialogBuilder.create();

			alertD.show();   	
				
        	
        }
        return super.onOptionsItemSelected(item);
    }



}