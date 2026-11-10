package esp.killshot;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.provider.Settings;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import esp.killshot.R;
import esp.killshot.launcher.FloatingModMenuService;

public class MainActivity extends Activity {

  public String GameActivity = "com.unity3d.player.UnityPlayerActivity";
  public boolean hasLaunched = false;
  LinearLayout linear;

  static {
    System.loadLibrary("GamerKillshot");
  }

  @Override
  protected void onCreate(Bundle bundle) {
    super.onCreate(bundle);
    setContentView(R.layout.activity_main);
    LinearLayout[] layouts = { (LinearLayout) findViewById(R.id.layout1),
        (LinearLayout) findViewById(R.id.layout2), (LinearLayout) findViewById(R.id.layout3) };

    for (int i = 0; i < layouts.length; i++) {

      final TextView subtitle = (TextView) layouts[i].findViewById(R.id.subtitle);

      final TextView heading = (TextView) layouts[i].findViewById(R.id.textheading);

      final TextView code = (TextView) layouts[i].findViewById(R.id.textcode);

      final TextView copy = (TextView) layouts[i].findViewById(R.id.btncopy);

      switch (i) {
        case 0:
          subtitle.setText("1. System Floating Window Permission "
              + "(Add the below code in AndroidManifest.xml)");
          heading.setText("Floating Permission");
          code.setText(
              "<uses-permission android:name=\"" + "android.permission.SYSTEM_ALERT_WINDOW\"/>");
          break;
        case 1:
          subtitle.setText("2. Service of Mod Menu " + "(Add the below code above the end of "
              + "</application> tag in AndroidManifest.xml)");
          heading.setText("Service Permission");
          code.setText(
              "<service\n" + "    android:name=\"esp.killshot.launcher.FloatingModMenuService\"\n"
                  + "    android:enabled=\"true\"\n" + "    android:exported=\"false\"\n"
                  + "    android:stopWithTask=\"true\">\n" + "</service>");
          break;
        case 2:
          subtitle
              .setText("3. MainActivity of Game " + "(Search onCreate method and paste the code)");
          heading.setText("MainActivity Text");
          code.setText("invoke-static {p0}, " + "Lesp/killshot/MainActivity;->"
              + "Start(Landroid/content/Context;)V");
          break;
      }
      copy.setText("❏ Copy Code");
      copy.setOnClickListener(new View.OnClickListener() {
        @Override
        public void onClick(View view) {
          ClipboardManager clipboard = (ClipboardManager) getSystemService(
              Context.CLIPBOARD_SERVICE);
          clipboard.setPrimaryClip(ClipData.newPlainText(null, code.getText()));
          copy.setText("✓ Copied!");
          new Handler().postDelayed(new Runnable() {
            @Override
            public void run() {
              copy.setText("❏ Copy Code");
            }

          }, 2000);
          Toast.makeText(MainActivity.this, "Text Copied To Clipboard", Toast.LENGTH_SHORT).show();
        }
      });
    }
    // Launch mod menu (only if the overlay permission is already granted)
    startMenuIfAllowed();
    // Launch Unity game activity, if this APK actually contains one
    if (!hasLaunched) {
      try {
        Intent intent = new Intent(MainActivity.this, Class.forName(GameActivity));
        startActivity(intent);
        hasLaunched = true;
      } catch (ClassNotFoundException e) {
        // Standalone tester build: there is no game activity, just show the menu
      }
    }
  }

  @Override
  protected void onResume() {
    super.onResume();
    // User may have just come back from the "Draw over other apps" settings screen
    startMenuIfAllowed();
  }

  private void startMenuIfAllowed() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
      if (!permissionAsked) {
        permissionAsked = true;
        Toast.makeText(this, "Allow \"Display over other apps\" for this app, then come back.",
            Toast.LENGTH_LONG).show();
        Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
            Uri.parse("package:" + getPackageName()));
        startActivity(intent);
      }
      return;
    }
    Start(this);
  }

  private boolean permissionAsked = false;

  private GradientDrawable gdnew(int color, int topLeft, int topRight, int bottomRight,
      int bottomLeft) {
    GradientDrawable drawable = new GradientDrawable();
    drawable.setColor(color);
    drawable.setCornerRadii(new float[] { topLeft, topLeft, topRight, topRight, bottomRight,
        bottomRight, bottomLeft, bottomLeft });
    return drawable;
  }

  public static void Start(final Context context) {
    // Check overlay permission
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(context)) {
      Toast.makeText(context.getApplicationContext(),
          "Overlay permission is required to show mod menu. "
              + "Allow permission and restart the game.",
          Toast.LENGTH_LONG).show();
      Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
          Uri.parse("package:" + context.getPackageName()));
      intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
      context.startActivity(intent);
      new Handler().postDelayed(new Runnable() {
        @Override
        public void run() {
          System.exit(1);
        }
      }, 5000);
      return;
    }
    // Start FloatingModMenuService
    new Handler().postDelayed(new Runnable() {
      @Override
      public void run() {
        Intent serviceIntent = new Intent(context, FloatingModMenuService.class);
        // startForegroundService() requires the service to call startForeground() within 5s,
        // which this service never does -> crash. Plain startService() is correct.
        context.startService(serviceIntent);
      }
    }, 500);
  }
}
