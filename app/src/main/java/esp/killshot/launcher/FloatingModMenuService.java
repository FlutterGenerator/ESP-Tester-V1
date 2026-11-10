package esp.killshot.launcher;

import android.app.ActivityManager;
import android.app.ActivityManager.RunningAppProcessInfo;
import android.app.AlertDialog;
import android.app.Service;
import android.content.*;
import android.content.res.ColorStateList;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.provider.Settings;
import android.text.Html;
import android.text.InputFilter;
import android.text.TextUtils;
import android.text.method.DigitsKeyListener;
import android.text.method.HideReturnsTransformationMethod;
import android.text.method.PasswordTransformationMethod;
import android.util.Base64;
import android.util.Log;
import android.util.TypedValue;
import android.view.*;
import android.view.inputmethod.InputMethodManager;
import android.webkit.WebView;
import android.widget.*;
import java.util.Arrays;
import java.util.LinkedList;
import java.util.Objects;

import static android.view.ViewGroup.LayoutParams.MATCH_PARENT;
import static android.view.ViewGroup.LayoutParams.WRAP_CONTENT;
import static android.widget.RelativeLayout.ALIGN_PARENT_LEFT;
import static android.widget.RelativeLayout.ALIGN_PARENT_RIGHT;

//Please don't replace listeners with lambda!
public class FloatingModMenuService extends Service {
  public static final String TAG = "Mod_Menu";

  // ********** Menu appearance **********//
  int TEXT_COLOR = Color.parseColor("#FFFFFF");
  int TEXT_COLOR_2 = Color.parseColor("#FFFFFF");
  int TEXT = Color.parseColor("#000000");
  int BTN_COLOR = Color.parseColor("#1C262D");
  int MENU_BG_COLOR = Color.parseColor("#000000");
  int MENU_FEATURE_BG_COLOR = Color.parseColor("#000000");
  int MENU_WIDTH = 240;
  int MENU_HEIGHT = 230;
  float MENU_CORNER = 1.0f;
  int ICON_SIZE = 50;
  float ICON_ALPHA = 0.7f; // was 10.7f (alpha is 0..1)
  int ToggleON = Color.GREEN;
  int ToggleOFF = Color.RED;
  int BtnON = Color.parseColor("#1b5e20");
  int BtnOFF = Color.parseColor("#7f0000");
  int CategoryBG = Color.parseColor("#FFFFFF");
  int SeekBarColor = Color.parseColor("#80CBC4");
  int SeekBarProgressColor = Color.parseColor("#80CBC4");
  int CheckBoxColor = Color.parseColor("#80CBC4");
  int RadioColor = Color.parseColor("#FFFFFF");
  String NumberTxtColor = "#FFFFFF";
  // *************************************//

  private WindowManager.LayoutParams espParams;
  LinearLayout mCollapse;
  RelativeLayout mCollapsed;
  LinearLayout mExpanded;
  RelativeLayout mRootContainer;
  LinearLayout mSettings;
  WindowManager mWindowManager;
  private ESPView overlayView;
  WindowManager.LayoutParams params;
  LinearLayout patches;
  FrameLayout rootFrame;
  LinearLayout.LayoutParams scrlLL;
  LinearLayout.LayoutParams scrlLLExpanded;
  ScrollView scrollView;
  ImageView startimage;
  boolean stopChecking;
  WebView wViewIcon;
  Button settingsBtn;
  boolean settingsOpen;

  // Load the native library here too: when the menu is injected into a game, MainActivity is not
  // used, so without this the natives (Icon(), getFeatureList()...) are not found.
  static {
    try {
      System.loadLibrary("GamerKillshot");
    } catch (UnsatisfiedLinkError e) {
      android.util.Log.e("ESP", "Cannot load libGamerKillshot.so: " + e.getMessage());
      throw e;
    }
  }

  // initialize methods from the native library
  public static native void DrawOn(ESPView espView, Canvas canvas);

  native String Eyeclose();

  native String Eyeopen();

  native String Icon();

  native String IconWebViewData();

  native String[] getFeatureList();

  native boolean isGameLibLoaded();

  native void setHeadingText(TextView textView);

  native void setTitleText(TextView textView);

  native String[] settingsList();

  private int getLayoutType() {
    if (Build.VERSION.SDK_INT >= 26) {
      return 2038; // TYPE_APPLICATION_OVERLAY
    }
    if (Build.VERSION.SDK_INT >= 24) {
      return 2002; // TYPE_PHONE
    }
    if (Build.VERSION.SDK_INT >= 23) {
      return 2005; // TYPE_TOAST
    }
    return 2003; // TYPE_SYSTEM_ALERT
  }

  @Override
  public void onCreate() {
    super.onCreate();
    Preferences.context = this;
    mWindowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
    overlayView = new ESPView(this);
    // ESP overlay first, so the menu window is added on top of it
    DrawCanvas();
    initFloating();

    final Handler handler = new Handler();
    handler.post(new Runnable() {
      @Override
      public void run() {
        Thread();
        handler.postDelayed(this, 1000);
      }
    });
  }

  private void DrawCanvas() {
    // 56 = FLAG_NOT_FOCUSABLE | FLAG_NOT_TOUCHABLE | FLAG_NOT_TOUCH_MODAL
    // + FLAG_LAYOUT_IN_SCREEN (256) + FLAG_LAYOUT_NO_LIMITS (512) so the canvas covers the whole
    // screen and canvas coordinates match screen coordinates (no 100px shift).
    espParams = new WindowManager.LayoutParams(-1, -1, getLayoutType(), 56 | 256 | 512, -3);
    espParams.gravity = 8388659; // TOP | START
    espParams.x = 0;
    espParams.y = 0;
    mWindowManager.addView(overlayView, espParams);
  }

  // Reconstructed: the decompiler failed on this method (JADX ArrayIndexOutOfBounds).
  private void initFloating() {
    rootFrame = new FrameLayout(this);
    rootFrame.setOnTouchListener(onTouchListener());
    mRootContainer = new RelativeLayout(this);
    mCollapsed = new RelativeLayout(this);
    mCollapsed.setVisibility(View.VISIBLE);
    mCollapsed.setAlpha(ICON_ALPHA);

    // ********** The box of the mod menu **********
    mExpanded = new LinearLayout(this);
    mExpanded.setVisibility(View.GONE);
    // White border around the whole menu (children are inset by the border width)
    GradientDrawable menuBorder = new GradientDrawable();
    menuBorder.setColor(MENU_BG_COLOR);
    menuBorder.setStroke(3, Color.WHITE);
    mExpanded.setBackground(menuBorder);
    mExpanded.setPadding(3, 3, 3, 3);
    mExpanded.setOrientation(LinearLayout.VERTICAL);
    mExpanded.setLayoutParams(new LinearLayout.LayoutParams(dp(MENU_WIDTH), WRAP_CONTENT));

    // ********** The icon to open mod menu **********
    startimage = new ImageView(this);
    startimage.setLayoutParams(new RelativeLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT));
    int iconPx = (int) TypedValue.applyDimension(1, ICON_SIZE, getResources().getDisplayMetrics());
    startimage.getLayoutParams().height = iconPx;
    startimage.getLayoutParams().width = iconPx;
    startimage.setScaleType(ImageView.ScaleType.FIT_XY);
    byte[] decode = Base64.decode(Icon(), 0);
    startimage.setImageBitmap(BitmapFactory.decodeByteArray(decode, 0, decode.length));
    ((ViewGroup.MarginLayoutParams) startimage.getLayoutParams()).topMargin = convertDipToPixels(
        10);
    startimage.setOnTouchListener(onTouchListener());
    startimage.setOnClickListener(new View.OnClickListener() {
      public void onClick(View view) {
        mCollapsed.setVisibility(View.GONE);
        mExpanded.setVisibility(View.VISIBLE);
      }
    });

    // ********** The icon in Webview to open mod menu **********
    final WebView wView = new WebView(this);
    wViewIcon = wView;
    wView.setLayoutParams(new RelativeLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT));
    wView.getLayoutParams().height = iconPx;
    wView.getLayoutParams().width = iconPx;
    wView.loadData("<html><head></head><body style=\"margin: 0; padding: 0\">" + "<img src=\""
        + IconWebViewData() + "\" width=\"" + ICON_SIZE + "\" height=\"" + ICON_SIZE + "\" >"
        + "</body></html>", "text/html", "utf-8");
    wView.setBackgroundColor(0x00000000);
    wView.setAlpha(ICON_ALPHA);
    wView.setOnTouchListener(onTouchListener());

    // ********** Settings icon **********
    TextView settings = new TextView(this);
    settings.setText("✯");
    settings.setTextColor(TEXT_COLOR);
    settings.setTypeface(Typeface.DEFAULT_BOLD);
    settings.setTextSize(16.0f);
    RelativeLayout.LayoutParams rlsettings = new RelativeLayout.LayoutParams(WRAP_CONTENT,
        WRAP_CONTENT);
    rlsettings.addRule(ALIGN_PARENT_RIGHT);
    rlsettings.addRule(RelativeLayout.CENTER_VERTICAL);
    settings.setLayoutParams(rlsettings);
    settings.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View v) {
        // Star in the title: hide the menu AND make the floating icon invisible.
        // Tap the same spot again to bring the icon (and menu) back.
        mCollapsed.setVisibility(View.VISIBLE);
        mCollapsed.setAlpha(0);
        mExpanded.setVisibility(View.GONE);
        Toast.makeText(view_ctx(v), "Icon hidden. Remember the hidden icon position",
            Toast.LENGTH_LONG).show();
      }
    });
    settings.setPadding(dp(6), dp(4), dp(6), dp(4));

    // ********** Settings **********
    mSettings = new LinearLayout(this);
    mSettings.setOrientation(LinearLayout.VERTICAL);
    featureList(settingsList(), mSettings);

    // ********** Title text **********
    RelativeLayout titleText = new RelativeLayout(this);
    titleText.setPadding(10, 5, 10, 5);
    titleText.setVerticalGravity(16);

    TextView title = new TextView(this);
    title.setTextColor(TEXT_COLOR);
    title.setTextSize(20.0f);
    title.setGravity(Gravity.CENTER);
    // NOT setSingleLine(true): it turns on horizontal scrolling, which stops auto-size from shrinking the text
    title.setMaxLines(1);
    title.setHorizontallyScrolling(false);
    title.setEllipsize(TextUtils.TruncateAt.END);
    // keep the text clear of the star on the right (same padding on the left so it stays centered)
    title.setPadding(dp(2), 0, dp(24), 0);
    if (Build.VERSION.SDK_INT >= 26) {
      title.setAutoSizeTextTypeUniformWithConfiguration(12, 22, 1, TypedValue.COMPLEX_UNIT_SP);
    }
    RelativeLayout.LayoutParams rl = new RelativeLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT);
    title.setLayoutParams(rl);
    setTitleText(title);

    // ********** Heading text **********
    TextView heading = new TextView(this);
    heading.setEllipsize(TextUtils.TruncateAt.MARQUEE);
    heading.setMarqueeRepeatLimit(-1);
    heading.setSingleLine(true);
    heading.setSelected(true);
    heading.setTextColor(TEXT_COLOR);
    heading.setTextSize(10.0f);
    heading.setGravity(Gravity.CENTER);
    heading.setPadding(0, 0, 0, 5);
    setHeadingText(heading);

    // ********** Mod menu feature list **********
    scrollView = new ScrollView(this);
    scrlLL = new LinearLayout.LayoutParams(MATCH_PARENT, dp(MENU_HEIGHT));
    scrlLLExpanded = new LinearLayout.LayoutParams(mExpanded.getLayoutParams());
    scrlLLExpanded.weight = 1.0f;
    scrollView.setLayoutParams(Preferences.isExpanded ? scrlLLExpanded : scrlLL);
    scrollView.setBackgroundColor(MENU_FEATURE_BG_COLOR);
    patches = new LinearLayout(this);
    patches.setOrientation(LinearLayout.VERTICAL);

    // ********** RelativeLayout for buttons **********
    RelativeLayout relativeLayout = new RelativeLayout(this);
    relativeLayout.setPadding(0, 0, 0, 0); // buttons sit flush with the menu border
    relativeLayout.setVerticalGravity(Gravity.CENTER);

    // ********** Hide/Kill button **********
    RelativeLayout.LayoutParams lParamsHideBtn = new RelativeLayout.LayoutParams(WRAP_CONTENT,
        WRAP_CONTENT);
    lParamsHideBtn.addRule(ALIGN_PARENT_LEFT);

    Button hideBtn = new Button(this);
    hideBtn.setLayoutParams(lParamsHideBtn);
    hideBtn.setBackground(bottomBtnBg(false));
    hideBtn.setText("SETTINGS");
    settingsBtn = hideBtn;
    hideBtn.setTextColor(TEXT_COLOR);
    compactBottomButton(hideBtn);
    hideBtn.setOnClickListener(new View.OnClickListener() {
      public void onClick(View view) {
        toggleSettings(!settingsOpen);
      }
    });
    hideBtn.setOnLongClickListener(new View.OnLongClickListener() {
      public boolean onLongClick(View view) {
        Toast.makeText(view.getContext(), "Menu service killed", Toast.LENGTH_LONG).show();
        FloatingModMenuService.this.stopSelf();
        return false;
      }
    });

    // ********** Close button **********
    RelativeLayout.LayoutParams lParamsCloseBtn = new RelativeLayout.LayoutParams(WRAP_CONTENT,
        WRAP_CONTENT);
    lParamsCloseBtn.addRule(ALIGN_PARENT_RIGHT);

    Button closeBtn = new Button(this);
    closeBtn.setLayoutParams(lParamsCloseBtn);
    closeBtn.setBackground(bottomBtnBg(true));
    closeBtn.setText("MINIMIZE");
    closeBtn.setTextColor(TEXT_COLOR);
    compactBottomButton(closeBtn);
    closeBtn.setOnClickListener(new View.OnClickListener() {
      public void onClick(View view) {
        mCollapsed.setVisibility(View.VISIBLE);
        mCollapsed.setAlpha(ICON_ALPHA);
        mExpanded.setVisibility(View.GONE);
      }
    });

    // ********** Window params of the menu **********
    params = new WindowManager.LayoutParams(WRAP_CONTENT, WRAP_CONTENT, getLayoutType(), 8, -3);
    params.gravity = 51;
    params.x = 0;
    params.y = 100;

    // ********** Adding view components **********
    rootFrame.addView(mRootContainer);
    mRootContainer.addView(mCollapsed);
    mRootContainer.addView(mExpanded);
    if (IconWebViewData() != null) {
      mCollapsed.addView(wView);
    } else {
      mCollapsed.addView(startimage);
    }
    titleText.addView(title);
    titleText.addView(settings);
    mExpanded.addView(titleText);
    mExpanded.addView(heading);
    scrollView.addView(patches);
    mExpanded.addView(scrollView);
    relativeLayout.addView(hideBtn);
    relativeLayout.addView(closeBtn);
    mExpanded.addView(relativeLayout);
    mWindowManager.addView(rootFrame, params);

    final Handler handler = new Handler();
    handler.postDelayed(new Runnable() {
      boolean viewLoaded = false;

      @Override
      public void run() {
        if (Preferences.loadPref && !isGameLibLoaded() && !stopChecking) {
          if (!viewLoaded) {
            patches.addView(Category(
                "Save preferences was been enabled. Waiting for game lib to be loaded...\n\nForce load menu may not apply mods instantly. You would need to reactivate them again"));
            patches.addView(Button(-100, "Force load menu"));
            viewLoaded = true;
          }
          handler.postDelayed(this, 600);
        } else {
          patches.removeAllViews();
          featureList(getFeatureList(), patches);
        }
      }
    }, 500);
  }

  private View.OnTouchListener onTouchListener() {
    return new View.OnTouchListener() {
      final View collapsedView = mCollapsed;
      final View expandedView = mExpanded;
      private float initialTouchX;
      private float initialTouchY;
      private int initialX;
      private int initialY;

      public boolean onTouch(View view, MotionEvent motionEvent) {
        switch (motionEvent.getAction()) {
          case MotionEvent.ACTION_DOWN:
            initialX = params.x;
            initialY = params.y;
            initialTouchX = motionEvent.getRawX();
            initialTouchY = motionEvent.getRawY();
            return true;
          case MotionEvent.ACTION_UP:
            int rawX = (int) (motionEvent.getRawX() - initialTouchX);
            int rawY = (int) (motionEvent.getRawY() - initialTouchY);
            mExpanded.setAlpha(1.0f);
            mCollapsed.setAlpha(1.0f);
            if (rawX < 10 && rawY < 10 && isViewCollapsed()) {
              try {
                collapsedView.setVisibility(View.GONE);
                expandedView.setVisibility(View.VISIBLE);
              } catch (NullPointerException e) {
              }
            }
            return true;
          case MotionEvent.ACTION_MOVE:
            mExpanded.setAlpha(0.5f);
            mCollapsed.setAlpha(0.5f);
            params.x = initialX + ((int) (motionEvent.getRawX() - initialTouchX));
            params.y = initialY + ((int) (motionEvent.getRawY() - initialTouchY));
            mWindowManager.updateViewLayout(rootFrame, params);
            return true;
          default:
            return false;
        }
      }
    };
  }

  private void featureList(String[] listFT, LinearLayout linearLayout) {
    int featNum;
    int subFeat = 0;
    LinearLayout llBak = linearLayout;

    for (int i = 0; i < listFT.length; i++) {
      boolean switchedOn = false;
      String feature = listFT[i];
      if (feature.contains("True_")) {
        switchedOn = true;
        feature = feature.replaceFirst("True_", "");
      }

      linearLayout = llBak;
      if (feature.contains("CollapseAdd_")) {
        linearLayout = mCollapse;
        feature = feature.replaceFirst("CollapseAdd_", "");
      }
      String[] str = feature.split("_");

      if (TextUtils.isDigitsOnly(str[0]) || str[0].matches("-[0-9]*")) {
        featNum = Integer.parseInt(str[0]);
        feature = feature.replaceFirst(str[0] + "_", "");
        subFeat++;
      } else {
        featNum = i - subFeat;
      }
      String[] strSplit = feature.split("_");
      String type = strSplit[0];
      if (type.equals("Toggle")) {
        linearLayout.addView(Switch(featNum, strSplit[1], switchedOn));
      } else if (type.equals("SeekBar")) {
        linearLayout.addView(SeekBar(featNum, strSplit[1], Integer.parseInt(strSplit[2]),
            Integer.parseInt(strSplit[3])));
      } else if (type.equals("Button")) {
        linearLayout.addView(Button(featNum, strSplit[1]));
      } else if (type.equals("ButtonOnOff")) {
        linearLayout.addView(ButtonOnOff(featNum, strSplit[1], switchedOn));
      } else if (type.equals("Spinner")) {
        linearLayout.addView(RichTextView(strSplit[1]));
        linearLayout.addView(Spinner(featNum, strSplit[1], strSplit[2]));
      } else if (type.equals("InputText")) {
        linearLayout.addView(TextField(featNum, strSplit[1], false, 0));
      } else if (type.equals("InputValue")) {
        if (strSplit.length == 3)
          linearLayout
              .addView(TextField(featNum, strSplit[2], true, Integer.parseInt(strSplit[1])));
        if (strSplit.length == 2)
          linearLayout.addView(TextField(featNum, strSplit[1], true, 0));
      } else if (type.equals("CheckBox")) {
        linearLayout.addView(CheckBox(featNum, strSplit[1], switchedOn));
      } else if (type.equals("RadioButton")) {
        linearLayout.addView(RadioButton(featNum, strSplit[1], strSplit[2]));
      } else if (type.equals("Collapse")) {
        Collapse(linearLayout, strSplit[1]);
        subFeat++;
      } else if (type.equals("ButtonLink")) {
        subFeat++;
        linearLayout.addView(ButtonLink(strSplit[1], strSplit[2]));
      } else if (type.equals("Category")) {
        subFeat++;
        linearLayout.addView(Category(strSplit[1]));
      } else if (type.equals("RichTextView")) {
        subFeat++;
        linearLayout.addView(RichTextView(strSplit[1]));
      } else if (type.equals("RichWebView")) {
        subFeat++;
        linearLayout.addView(RichWebView(strSplit[1]));
      } else if (type.equals("Devic")) {
        subFeat++;
        linearLayout.addView(Devic(strSplit[1]));
      } else if (type.equals("ID")) {
        subFeat++;
        linearLayout.addView(Device(strSplit[1]));
      }
    }
  }

  private View Switch(final int featNum, final String featName, boolean swiOn) {
    LinearLayout row = new LinearLayout(this);
    row.setOrientation(LinearLayout.HORIZONTAL);
    row.setLayoutParams(new LinearLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT));

    TextView star = new TextView(this);
    star.setText("✯");
    star.setTextColor(TEXT_COLOR);
    star.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View view) {
        Preferences.changeFeatureInt(featName, featNum, 0);
      }
    });

    final GradientDrawable thumb = new GradientDrawable();
    thumb.setSize(dp(20), dp(20));
    thumb.setShape(GradientDrawable.OVAL);
    final GradientDrawable track = new GradientDrawable();
    track.setSize(dp(45), dp(20));
    track.setCornerRadius(100);
    if (swiOn) {
      thumb.setStroke(3, Color.parseColor("#FFFFFF"));
      thumb.setColor(Color.BLACK);
      track.setStroke(3, Color.parseColor("#FFFFFF"));
      track.setColor(Color.BLACK);
    } else {
      thumb.setStroke(3, Color.BLACK);
      thumb.setColor(Color.parseColor("#C5FFFFFF"));
      track.setStroke(3, Color.BLACK);
      track.setColor(Color.parseColor("#C5FFFFFF"));
    }

    final Switch switchR = new Switch(this);
    switchR.setText(featName);
    switchR.setTextColor(TEXT_COLOR_2);
    switchR.setPadding(10, 10, 20, 10);
    switchR.setThumbDrawable(thumb);
    switchR.setLayoutParams(new LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT));
    switchR.setTrackDrawable(track);
    switchR.setChecked(Preferences.loadPrefBool(featName, featNum, swiOn));
    switchR.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
      // Reconstructed from the smali dump (decompiler failed here)
      public void onCheckedChanged(CompoundButton compoundButton, boolean bool) {
        Preferences.changeFeatureBool(featName, featNum, bool);
        if (bool) {
          thumb.setStroke(3, Color.parseColor("#FFFFFF"));
          thumb.setColor(Color.BLACK);
          track.setStroke(3, Color.parseColor("#FFFFFF"));
          track.setColor(Color.BLACK);
        } else {
          thumb.setStroke(3, Color.BLACK);
          thumb.setColor(Color.parseColor("#C5FFFFFF"));
          track.setStroke(3, Color.BLACK);
          track.setColor(Color.parseColor("#C5FFFFFF"));
        }
        switch (featNum) {
          case -1: // Save preferences
            Preferences.with(switchR.getContext()).writeBoolean(-1, bool);
            if (!bool)
              Preferences.with(switchR.getContext()).clear();
            break;
          case -3: // Auto size vertically
            Preferences.isExpanded = bool;
            scrollView.setLayoutParams(bool ? scrlLLExpanded : scrlLL);
            break;
        }
      }
    });
    row.addView(star);
    row.addView(switchR);
    return row;
  }

  private View SeekBar(final int featNum, final String featName, final int min, int max) {
    int loadedProg = Preferences.loadPrefInt(featName, featNum);
    LinearLayout linearLayout = new LinearLayout(this);
    linearLayout.setPadding(10, 5, 0, 5);
    linearLayout.setOrientation(LinearLayout.VERTICAL);
    linearLayout.setGravity(Gravity.CENTER);

    final TextView textView = new TextView(this);
    textView.setText(Html.fromHtml(featName + " -> <font color='#FFFFFF'>DEFAULT"));
    textView.setTextColor(TEXT_COLOR_2);

    SeekBar seekBar = new SeekBar(this);
    GradientDrawable thumb = new GradientDrawable();
    thumb.setShape(GradientDrawable.OVAL);
    thumb.setColor(Color.BLACK);
    thumb.setStroke(dp(2), Color.parseColor("#FFFFFF"));
    thumb.setCornerRadius(15.0f);
    thumb.setSize(dp(20), dp(20));
    seekBar.setThumb(thumb);
    seekBar.getProgressDrawable().setColorFilter(Color.parseColor("#FFFFFF"),
        PorterDuff.Mode.SRC_IN);
    seekBar.setPadding(25, 10, 35, 10);
    seekBar.setMax(max);
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
      seekBar.setMin(min);
    seekBar.setProgress(loadedProg == 0 ? min : loadedProg);
    seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
      public void onStartTrackingTouch(SeekBar seekBar) {
      }

      public void onStopTrackingTouch(SeekBar seekBar) {
      }

      // Reconstructed (decompiler failed here)
      public void onProgressChanged(SeekBar seekBar, int i, boolean z) {
        int value = i < min ? min : i;
        if (value != i) {
          seekBar.setProgress(value);
        }
        Preferences.changeFeatureInt(featName, featNum, value);
        applyIconSetting(featNum, value);
        textView
            .setText(Html.fromHtml(featName + " -> <font color='" + NumberTxtColor + "'>" + value));
      }
    });
    if ((featNum == -7 || featNum == -8) && loadedProg != 0)
      applyIconSetting(featNum, loadedProg);
    linearLayout.addView(textView);
    linearLayout.addView(seekBar);
    return linearLayout;
  }

  // Smaller bottom buttons so they don't swallow the feature list
  private void compactBottomButton(Button b) {
    b.setAllCaps(true);
    b.setTextSize(16.0f);
    b.setTypeface(Typeface.DEFAULT_BOLD);
    b.setMinHeight(0);
    b.setMinimumHeight(0);
    b.setMinWidth(0);
    b.setMinimumWidth(0);
    b.setPadding(dp(12), dp(6), dp(12), dp(6));
    b.setSingleLine(true);
    b.setStateListAnimator(null);
    ViewGroup.LayoutParams lp = b.getLayoutParams();
    if (lp != null) {
      lp.height = dp(47);
      lp.width = ViewGroup.LayoutParams.WRAP_CONTENT; // narrow buttons in the corners, gap in the middle
    }
  }

  private GradientDrawable bottomBtnBg(boolean right) {
    GradientDrawable gd = new GradientDrawable();
    gd.setCornerRadii(right ? new float[] { 40, 40, 0, 0, 0, 0, 0, 0 }
        : new float[] { 0, 0, 40, 40, 0, 0, 0, 0 });
    gd.setColor(Color.parseColor("#000000"));
    gd.setStroke(3, -1);
    return gd;
  }

  // Opens/closes the settings page. The left bottom button shows SETTINGS / CLOSE accordingly.
  private void toggleSettings(boolean open) {
    if (open == settingsOpen)
      return;
    try {
      if (open) {
        scrollView.removeView(patches);
        scrollView.addView(mSettings);
        scrollView.scrollTo(0, 0);
      } else {
        scrollView.removeView(mSettings);
        scrollView.addView(patches);
      }
      settingsOpen = open;
      if (settingsBtn != null)
        settingsBtn.setText(open ? "CLOSE" : "SETTINGS");
    } catch (IllegalStateException e) {
    }
  }

  // Icon size (feature -7, 20..100 dp) and icon opacity (feature -8, 10..100 %)
  private void applyIconSetting(int featNum, int value) {
    if (featNum == -7) {
      int px = dp(value);
      if (startimage != null) {
        startimage.getLayoutParams().width = px;
        startimage.getLayoutParams().height = px;
        startimage.requestLayout();
      }
      if (wViewIcon != null) {
        wViewIcon.getLayoutParams().width = px;
        wViewIcon.getLayoutParams().height = px;
        wViewIcon.requestLayout();
      }
    } else if (featNum == -8) {
      ICON_ALPHA = value / 100f;
      if (mCollapsed != null && mCollapsed.getVisibility() == View.VISIBLE) {
        mCollapsed.setAlpha(ICON_ALPHA);
      }
    }
  }

  private GradientDrawable roundedBtnBg() {
    GradientDrawable gd = new GradientDrawable();
    gd.setCornerRadii(new float[] { 30, 30, 0, 0, 30, 30, 0, 0 });
    gd.setColor(Color.parseColor("#000000"));
    gd.setStroke(3, -1);
    return gd;
  }

  private View Button(final int featNum, final String featName) {
    final Button button = new Button(this);
    LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(MATCH_PARENT,
        MATCH_PARENT);
    layoutParams.setMargins(7, 5, 7, 5);
    button.setLayoutParams(layoutParams);
    button.setTextColor(TEXT_COLOR_2);
    button.setAllCaps(false);
    button.setText(Html.fromHtml(featName));
    button.setBackgroundColor(BTN_COLOR);
    button.setBackground(roundedBtnBg());
    button.setOnClickListener(new View.OnClickListener() {
      public void onClick(View v) {
        switch (featNum) {
          case -100:
            stopChecking = true;
            break;
          case -6:
            toggleSettings(false);
            break;
          case -5:
            try {
              Intent intent = new Intent();
              intent.setAction(Intent.ACTION_SEND);
              intent.putExtra(Intent.EXTRA_TEXT,
                  "Hi Guys, I have the esp tester of gamer killshot\nDownload Link: https://t.me/gamerkillshotchannel");
              intent.setType("text/plain");
              intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
              startActivity(Intent.createChooser(intent, "Share via")
                  .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            } catch (Exception e) {
              Toast.makeText(getApplicationContext(), e.toString(), Toast.LENGTH_SHORT).show();
            }
            break;
        }
        Preferences.changeFeatureInt(featName, featNum, 0);
      }
    });
    return button;
  }

  private View ButtonLink(final String featName, final String url) {
    final Button button = new Button(this);
    LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(MATCH_PARENT,
        MATCH_PARENT);
    layoutParams.setMargins(7, 5, 7, 5);
    button.setLayoutParams(layoutParams);
    button.setAllCaps(false);
    button.setTextColor(TEXT_COLOR_2);
    button.setText(Html.fromHtml(featName));
    button.setBackgroundColor(BTN_COLOR);
    button.setBackground(roundedBtnBg());
    button.setOnClickListener(new View.OnClickListener() {
      public void onClick(View v) {
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.setData(Uri.parse(url));
        startActivity(intent);
      }
    });
    return button;
  }

  private View ButtonOnOff(final int featNum, String featName, boolean switchedOn) {
    final Button button = new Button(this);
    LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(MATCH_PARENT,
        MATCH_PARENT);
    layoutParams.setMargins(7, 5, 7, 5);
    button.setLayoutParams(layoutParams);
    button.setTextColor(TEXT_COLOR_2);
    button.setAllCaps(false);

    final String finalfeatName = featName.replace("OnOff_", "");
    boolean isOn = Preferences.loadPrefBool(featName, featNum, switchedOn);
    if (isOn) {
      button.setText(Html.fromHtml(finalfeatName + ": ON"));
      button.setBackgroundColor(BtnON);
      isOn = false;
    } else {
      button.setText(Html.fromHtml(finalfeatName + ": OFF"));
      button.setBackgroundColor(BtnOFF);
      isOn = true;
    }
    final boolean finalIsOn = isOn;
    button.setOnClickListener(new View.OnClickListener() {
      boolean isOn = finalIsOn;

      @Override
      public void onClick(View v) {
        Preferences.changeFeatureBool(finalfeatName, featNum, isOn);
        if (isOn) {
          button.setText(Html.fromHtml(finalfeatName + ": ON"));
          button.setBackgroundColor(BtnON);
          isOn = false;
        } else {
          button.setText(Html.fromHtml(finalfeatName + ": OFF"));
          button.setBackgroundColor(BtnOFF);
          isOn = true;
        }
      }
    });
    return button;
  }

  private View Spinner(final int featNum, final String featName, String list) {
    Log.d(TAG, "spinner " + featNum + " " + featName + " " + list);
    LinkedList<String> lists = new LinkedList<String>(Arrays.asList(list.split(",")));

    LinearLayout linearLayout = new LinearLayout(this);
    LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(MATCH_PARENT,
        WRAP_CONTENT);
    layoutParams.setMargins(7, 2, 7, 5);
    linearLayout.setOrientation(LinearLayout.VERTICAL);
    linearLayout.setBackgroundColor(BTN_COLOR);
    linearLayout.setLayoutParams(layoutParams);

    final Spinner spinner = new Spinner(this, Spinner.MODE_DROPDOWN);
    spinner.setLayoutParams(layoutParams);
    spinner.getBackground().setColorFilter(1, PorterDuff.Mode.SRC_ATOP);
    ArrayAdapter<String> aa = new ArrayAdapter<String>(this,
        android.R.layout.simple_spinner_dropdown_item, lists);
    aa.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
    spinner.setAdapter(aa);
    spinner.setSelection(Preferences.loadPrefInt(featName, featNum));
    spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
      @Override
      public void onNothingSelected(AdapterView<?> adapterView) {
      }

      @Override
      public void onItemSelected(AdapterView<?> adapterView, View view, int position, long id) {
        Preferences.changeFeatureInt(spinner.getSelectedItem().toString(), featNum, position);
        if (adapterView.getChildAt(0) != null) {
          ((TextView) adapterView.getChildAt(0)).setTextColor(TEXT_COLOR_2);
        }
      }
    });
    linearLayout.addView(spinner);
    return linearLayout;
  }

  private View TextField(final int featNum, final String featName, final boolean numOnly,
      final int maxValue) {
    final EditTextString edittextstring = new EditTextString();
    final EditTextNum edittextnum = new EditTextNum();
    LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(MATCH_PARENT,
        WRAP_CONTENT);
    layoutParams.setMargins(20, 10, 15, 10);

    LinearLayout linearLayout = new LinearLayout(this);
    linearLayout.setOrientation(LinearLayout.HORIZONTAL);
    linearLayout.setGravity(16);
    linearLayout.setLayoutParams(layoutParams);
    linearLayout.setPadding(20, 10, 10, 10);
    GradientDrawable border = new GradientDrawable();
    border.setStroke(5, -1);
    border.setCornerRadii(new float[] { 30.0f, 30.0f, 0.0f, 0.0f, 30.0f, 30.0f, 0.0f, 0.0f });
    linearLayout.setBackground(border);

    LinearLayout linearLayout2 = new LinearLayout(this);
    linearLayout2.setOrientation(LinearLayout.VERTICAL);
    linearLayout2.setGravity(16);

    TextView textView = new TextView(this);
    textView.setTextColor(-1);
    textView.setText(featName);
    textView.setTextSize(14.0f);
    textView.setGravity(3);
    textView.setPadding(10, 0, 0, 5);

    LinearLayout linearLayout3 = new LinearLayout(this);
    linearLayout3.setOrientation(LinearLayout.HORIZONTAL);
    linearLayout3.setGravity(16);

    final TextView textView2 = new TextView(this);
    textView2.setText("-> ");
    textView2.setTextColor(-1);
    textView2.setTextSize(14.0f);
    textView2.setGravity(3);
    textView2.setPadding(10, 0, 0, 5);

    final ImageView imageView = new ImageView(this);
    LinearLayout.LayoutParams iconLp = new LinearLayout.LayoutParams(dp(30), dp(30));
    iconLp.gravity = 80;
    imageView.setLayoutParams(iconLp);
    imageView.setPadding(dp(4), dp(4), dp(4), dp(4));
    final byte[] eyeOpen = Base64.decode(Eyeopen(), 0);
    final byte[] eyeClose = Base64.decode(Eyeclose(), 0);
    imageView.setImageBitmap(BitmapFactory.decodeByteArray(eyeOpen, 0, eyeOpen.length));
    imageView.setOnClickListener(new View.OnClickListener() {
      boolean hidden;

      @Override
      public void onClick(View view) {
        try {
          hidden = !hidden;
          if (hidden) {
            imageView.setImageBitmap(BitmapFactory.decodeByteArray(eyeClose, 0, eyeClose.length));
            textView2.setTransformationMethod(PasswordTransformationMethod.getInstance());
          } else {
            imageView.setImageBitmap(BitmapFactory.decodeByteArray(eyeOpen, 0, eyeOpen.length));
            textView2.setTransformationMethod(HideReturnsTransformationMethod.getInstance());
          }
        } catch (IllegalStateException e) {
        }
      }
    });

    Button button = new Button(this);
    if (numOnly) {
      int saved = Preferences.loadPrefInt(featName, featNum);
      edittextnum.setNum(saved == 0 ? 1 : saved);
    } else {
      String saved = Preferences.loadPrefString(featName, featNum);
      edittextstring.setString(saved == null ? "" : saved);
    }
    textView2.setText(Html.fromHtml("-> <font color='#FFFFFF'>DEFAULT</font>"));

    button.setAllCaps(false);
    button.setText("ENTER");
    GradientDrawable btnBg = new GradientDrawable();
    btnBg.setColor(-16777216);
    btnBg.setCornerRadii(new float[] { 30.0f, 30.0f, 0.0f, 0.0f, 30.0f, 30.0f, 0.0f, 0.0f });
    btnBg.setStroke(3, -1);
    button.setBackground(btnBg);
    button.setTextColor(-1);
    button.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View view) {
        showInputDialog(featName, featNum, numOnly, maxValue, edittextstring, edittextnum,
            textView2);
      }
    });

    LinearLayout.LayoutParams textLp = new LinearLayout.LayoutParams(0, WRAP_CONTENT);
    textLp.weight = 1.0f;
    textLp.gravity = Gravity.START;
    LinearLayout linearLayout4 = new LinearLayout(this);
    linearLayout4.setLayoutParams(textLp);
    linearLayout4.setGravity(Gravity.START);

    linearLayout2.addView(textView);
    linearLayout3.addView(textView2);
    linearLayout2.addView(linearLayout3);
    linearLayout4.addView(linearLayout2);
    linearLayout.addView(linearLayout4);
    linearLayout.addView(imageView);
    linearLayout.addView(button);
    return linearLayout;
  }

  private void showInputDialog(final String featName, final int featNum, final boolean numOnly,
      final int maxValue, final EditTextString edittextstring, final EditTextNum edittextnum,
      final TextView textView2) {
    final AlertDialog alert = new AlertDialog.Builder(getApplicationContext(), 2).create();
    if (Build.VERSION.SDK_INT >= 19) {
      ((Window) Objects.requireNonNull(alert.getWindow()))
          .setType(Build.VERSION.SDK_INT >= 26 ? 2038 : 2002);
    }
    alert.setOnCancelListener(new DialogInterface.OnCancelListener() {
      @Override
      public void onCancel(DialogInterface dialogInterface) {
        ((InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE)).toggleSoftInput(1, 0);
      }
    });

    GradientDrawable bg = new GradientDrawable();
    bg.setColor(Color.parseColor("#000000"));
    bg.setStroke(3, -1);
    bg.setCornerRadius(10);
    bg.setSize(50, 20);
    bg.setCornerRadii(new float[] { 0, 0, 0, 0, 0, 0, 0, 0 });

    LinearLayout linearLayout = new LinearLayout(getApplicationContext());
    linearLayout.setLayoutParams(new LinearLayout.LayoutParams(dp(7000), dp(200)));
    linearLayout.setPadding(3, 3, 3, 3);
    linearLayout.setOrientation(LinearLayout.VERTICAL);
    linearLayout.setBackground(bg);

    TextView title = new TextView(getApplicationContext());
    title.setGravity(17);
    title.setText(Html.fromHtml("<u>" + featName + "</u>"));
    title.setTextColor(-1);
    title.setTypeface(Typeface.DEFAULT_BOLD);
    title.setTextSize(20.0f);

    TextView hint = new TextView(getApplicationContext());
    hint.setText("Tap \"SET VALUE\" to apply changes || Tap outside to cancel");
    if (maxValue != 0) {
      hint.setText(
          "Tap \"SET VALUE\"to apply changes || Tap outside to cancel\nMax value: " + maxValue);
    }
    hint.setTextColor(-1);

    final EditText editText = new EditText(getApplicationContext());
    GradientDrawable editBg = new GradientDrawable();
    editBg.setColor(Color.parseColor("#000000"));
    editBg.setStroke(1, -1);
    editBg.setCornerRadii(new float[] { 120, 120, 120, 120, 120, 120, 120, 120 });
    editText.setBackgroundDrawable(editBg);
    editText.setTextColor(-1);
    if (numOnly) {
      editText.setInputType(2);
      editText.setKeyListener(DigitsKeyListener.getInstance("0123456789-"));
      editText.setFilters(new InputFilter[] { new InputFilter.LengthFilter(10) });
      editText.setHint("WRITE VALUE");
    } else {
      editText.setText(edittextstring.getString());
    }
    editText.setOnFocusChangeListener(new View.OnFocusChangeListener() {
      @Override
      public void onFocusChange(View v, boolean hasFocus) {
        InputMethodManager imm = (InputMethodManager) getSystemService(
            Context.INPUT_METHOD_SERVICE);
        if (hasFocus) {
          imm.toggleSoftInput(2, 1);
        } else {
          imm.toggleSoftInput(1, 0);
        }
      }
    });
    editText.requestFocus();

    GradientDrawable btnBg = new GradientDrawable();
    btnBg.setStroke(1, -1);
    btnBg.setColor(-16777216);
    btnBg.setCornerRadii(new float[] { 40, 40, 0, 0, 40, 40, 0, 0 });

    Button button = new Button(getApplicationContext());
    button.setTextColor(-1);
    button.setText("SET VALUE");
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT);
    lp.setMargins(7, 5, 7, 5);
    button.setLayoutParams(lp);
    button.setBackgroundDrawable(btnBg);
    button.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View v) {
        if (numOnly) {
          int num;
          try {
            String txt = editText.getText().toString();
            num = Integer.parseInt(TextUtils.isEmpty(txt) ? "0" : txt);
            if (maxValue != 0 && num >= maxValue) {
              num = maxValue;
            }
          } catch (NumberFormatException e) {
            num = Integer.MAX_VALUE - 7; // 2147483640
          }
          edittextnum.setNum(num);
          textView2
              .setText(Html.fromHtml("-> <font color='" + NumberTxtColor + "'>" + num + "</font>"));
          alert.dismiss();
          Preferences.changeFeatureInt(featName, featNum, num);
        } else {
          String str = editText.getText().toString();
          edittextstring.setString(str);
          textView2
              .setText(Html.fromHtml("-> <font color='" + NumberTxtColor + "'>" + str + "</font>"));
          alert.dismiss();
          Preferences.changeFeatureString(featName, featNum, str);
        }
        editText.setFocusable(false);
      }
    });

    linearLayout.addView(title);
    linearLayout.addView(hint);
    linearLayout.addView(editText);
    linearLayout.addView(button);
    alert.setView(linearLayout);
    alert.show();
  }

  private View CheckBox(final int featNum, final String featName, boolean switchedOn) {
    final CheckBox checkBox = new CheckBox(this);
    checkBox.setText(featName);
    checkBox.setTextColor(TEXT_COLOR_2);
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP)
      checkBox.setButtonTintList(ColorStateList.valueOf(CheckBoxColor));
    checkBox.setChecked(Preferences.loadPrefBool(featName, featNum, switchedOn));
    checkBox.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
      @Override
      public void onCheckedChanged(CompoundButton compoundButton, boolean z) {
        Preferences.changeFeatureBool(featName, featNum, z);
      }
    });
    return checkBox;
  }

  private View RadioButton(final int featNum, final String featName, String list) {
    final LinkedList<String> lists = new LinkedList<String>(Arrays.asList(list.split(",")));

    final TextView textView = new TextView(this);
    textView.setText(featName + ":");
    textView.setTextColor(TEXT_COLOR_2);

    final RadioGroup radioGroup = new RadioGroup(this);
    radioGroup.setPadding(10, 5, 10, 5);
    radioGroup.setOrientation(LinearLayout.VERTICAL);
    radioGroup.addView(textView);

    for (int i = 0; i < lists.size(); i++) {
      final RadioButton radioButton = new RadioButton(this);
      final String radioName = lists.get(i);
      View.OnClickListener first = new View.OnClickListener() {
        public void onClick(View v) {
          textView.setText(
              Html.fromHtml(featName + ": <font color='" + NumberTxtColor + "'>" + radioName));
          Preferences.changeFeatureInt(featName, featNum, radioGroup.indexOfChild(radioButton));
        }
      };
      radioButton.setText(radioName);
      radioButton.setTextColor(-3355444);
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP)
        radioButton.setButtonTintList(ColorStateList.valueOf(RadioColor));
      radioButton.setOnClickListener(first);
      radioGroup.addView(radioButton);
    }

    int index = Preferences.loadPrefInt(featName, featNum);
    if (index > 0) {
      textView.setText(Html
          .fromHtml(featName + ": <font color='" + NumberTxtColor + "'>" + lists.get(index - 1)));
      ((RadioButton) radioGroup.getChildAt(index)).setChecked(true);
    }
    return radioGroup;
  }

  private void Collapse(LinearLayout linLayout, final String text) {
    LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(MATCH_PARENT,
        MATCH_PARENT);
    layoutParams.setMargins(0, 5, 0, 0);
    LinearLayout collapse = new LinearLayout(this);
    collapse.setLayoutParams(layoutParams);
    collapse.setVerticalGravity(16);
    collapse.setOrientation(LinearLayout.VERTICAL);

    final LinearLayout collapseSub = new LinearLayout(this);
    collapseSub.setVerticalGravity(16);
    collapseSub.setPadding(0, 5, 0, 5);
    collapseSub.setOrientation(LinearLayout.VERTICAL);
    collapseSub.setBackgroundColor(Color.parseColor("#222D38"));
    collapseSub.setVisibility(View.GONE);
    mCollapse = collapseSub;

    final TextView textView = new TextView(this);
    textView.setBackgroundColor(CategoryBG);
    textView.setText("▽ " + text + " ▽");
    textView.setGravity(Gravity.CENTER);
    textView.setTextColor(TEXT_COLOR_2);
    textView.setTypeface(null, Typeface.BOLD);
    textView.setPadding(0, 20, 0, 20);
    textView.setOnClickListener(new View.OnClickListener() {
      boolean isChecked;

      @Override
      public void onClick(View v) {
        isChecked = !isChecked;
        if (isChecked) {
          collapseSub.setVisibility(View.VISIBLE);
          textView.setText("△ " + text + " △");
        } else {
          collapseSub.setVisibility(View.GONE);
          textView.setText("▽ " + text + " ▽");
        }
      }
    });
    collapse.addView(textView);
    collapse.addView(collapseSub);
    linLayout.addView(collapse);
  }

  private View Category(String text) {
    TextView textView = new TextView(this);
    textView.setBackgroundColor(CategoryBG);
    textView.setText(Html.fromHtml(text));
    textView.setGravity(Gravity.CENTER);
    textView.setTextColor(TEXT);
    textView.setTypeface(Typeface.SERIF);
    textView.setPadding(0, 5, 0, 5);
    return textView;
  }

  private View RichTextView(String text) {
    TextView textView = new TextView(this);
    textView.setText(Html.fromHtml(text));
    textView.setTextColor(TEXT_COLOR_2);
    textView.setPadding(10, 5, 10, 5);
    return textView;
  }

  private View RichWebView(String text) {
    WebView wView = new WebView(this);
    wView.loadData(text, "text/html", "utf-8");
    wView.setBackgroundColor(0x00000000);
    wView.setPadding(0, 5, 0, 5);
    return wView;
  }

  private View Devic(String text) {
    TextView textView = new TextView(this);
    textView.setText(" • Model-> " + Build.MODEL + "\n • Device-> " + Build.BRAND + "\n • API -> "
        + Build.VERSION.SDK_INT + "\n • UUID-> "
        + Settings.Secure.getString(getContentResolver(), "android_id")
        + "\n\n • Packeg Name -> Killshot.Testers" + "\n • Architecture -> " + Build.CPU_ABI
        + "\n • Phone name -> " + Build.ID + "\n • Serial number -> " + Build.SERIAL);
    textView.setTextColor(TEXT_COLOR_2);
    textView.setTextIsSelectable(true);
    textView.setPadding(4, 2, 4, 2);
    return textView;
  }

  private View Device(String text) {
    final TextView textView = new TextView(this);
    textView.setText("Your User ID -> YY" + Base64.encodeToString(Build.DISPLAY.getBytes(), 0));
    textView.setTextIsSelectable(true);
    textView.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View view) {
        Toast.makeText(view.getContext(), "COPIED TO CLIPBOARD", Toast.LENGTH_LONG).show();
        ((ClipboardManager) getApplicationContext().getSystemService(Context.CLIPBOARD_SERVICE))
            .setPrimaryClip(ClipData.newPlainText("Copied", textView.getText().toString()));
      }
    });
    textView.setTextColor(TEXT_COLOR_2);
    textView.setPadding(4, 2, 4, 2);
    return textView;
  }

  // Override our Start Command so the Service doesnt try to recreate itself when the App is closed
  @Override
  public int onStartCommand(Intent intent, int i, int i2) {
    return Service.START_NOT_STICKY;
  }

  private boolean isViewCollapsed() {
    return rootFrame == null || mCollapsed.getVisibility() == View.VISIBLE;
  }

  private int convertDipToPixels(int i) {
    return (int) ((i * getResources().getDisplayMetrics().density) + 0.5f);
  }

  private Context view_ctx(View v) {
    return v.getContext();
  }

  private int dp(int i) {
    return (int) TypedValue.applyDimension(1, i, getResources().getDisplayMetrics());
  }

  private boolean isNotInGame() {
    RunningAppProcessInfo info = new RunningAppProcessInfo();
    ActivityManager.getMyMemoryState(info);
    return info.importance != 100;
  }

  @Override
  public void onDestroy() {
    super.onDestroy();
    try {
      if (rootFrame != null) {
        mWindowManager.removeView(rootFrame);
      }
      if (overlayView != null) {
        overlayView.stop();
        mWindowManager.removeView(overlayView);
        overlayView = null;
      }
    } catch (IllegalArgumentException e) {
      Log.e(TAG, "View already removed: " + e.getMessage());
    }
  }

  @Override
  public void onTaskRemoved(Intent intent) {
    super.onTaskRemoved(intent);
    try {
      Thread.sleep(100);
    } catch (InterruptedException e) {
      e.printStackTrace();
    }
    stopSelf();
  }

  private void Thread() {
    if (rootFrame == null) {
      return;
    }
    rootFrame.setVisibility(isNotInGame() ? View.INVISIBLE : View.VISIBLE);
  }

  private class EditTextString {
    private String text;

    public void setString(String s) {
      text = s;
    }

    public String getString() {
      return text;
    }
  }

  private class EditTextNum {
    private int val;

    public void setNum(int i) {
      val = i;
    }

    public int getNum() {
      return val;
    }
  }

  @Override
  public IBinder onBind(Intent intent) {
    return null;
  }
}
