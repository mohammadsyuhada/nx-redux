#ifndef __msettings_h__
#define __msettings_h__

#define SETTINGS_DEFAULT_BRIGHTNESS 2
#define SETTINGS_DEFAULT_COLORTEMP 20
#define SETTINGS_DEFAULT_CONTRAST 0
#define SETTINGS_DEFAULT_SATURATION 0
#define SETTINGS_DEFAULT_EXPOSURE 0
#define SETTINGS_DEFAULT_VOLUME 8
#define SETTINGS_DEFAULT_HEADPHONE_VOLUME 4
#define SETTINGS_DEFAULT_FAN_SPEED 0
#define SETTINGS_DEFAULT_SOFTWARE_VOLUME 20

#define SETTINGS_DEFAULT_FN_NO_CHANGE -69

void InitSettings(void);
void QuitSettings(void);
int InitializedSettings(void);

int GetBrightness(void);
int GetColortemp(void);
int GetContrast(void);
int GetSaturation(void);
int GetExposure(void);
int GetVolume(void);
int GetGameVolume(void);
int GetMusicVolume(void);

void SetRawBrightness(int value); // 0-255
void SetRawColortemp(int value);  // 0-255
void SetRawContrast(int value);	  // 0-100
void SetRawSaturation(int value); // 0-100
void SetRawExposure(int value);	  // 0-100
void SetRawVolume(int value);	  // 0-100

void SetBrightness(int value); // 0-10
void SetColortemp(int value);  // 0-40
void SetContrast(int value);   // -4-5
void SetSaturation(int value); // -5-5
void SetExposure(int value);   // -4-5
void SetVolume(int value);	   // 0-20
void SetGameVolume(int value);
void SetMusicVolume(int value);

int GetJack(void);
void SetJack(int value); // 0-1

#define AUDIO_SINK_DEFAULT 0   // use system default, usually speaker (or jack if plugged in)
#define AUDIO_SINK_BLUETOOTH 1 // software control via bluealsa, not a separate card
#define AUDIO_SINK_USBDAC 2	   // assumes being exposed as card 1 to alsa
int GetAudioSink(void);
void SetAudioSink(int value);

int GetHDMI(void);
void SetHDMI(int value); // 0-1

int GetFnMode(void);
void SetFnMode(int value); // 0-1
// OSD output mute (settings->speaker_mute): pure output silence, independent of FN mode
int GetSpeakerMute(void);
void SetSpeakerMute(int value);
int GetRumble(void); // master motor switch, 1 = vibration allowed (default)
void SetRumble(int on);
int GetRumbleStrength(void); // 0 Normal (default), 1 Light, 2 Strong — see all/common/vib_levels.h
void SetRumbleStrength(int level);

// unused
inline int GetFanSpeed(void) {
	return 0;
}
inline void SetFanSpeed(int value) {
	// do nothing
}

// custom mute mode persistence layer

int GetFnBrightness(void);
int GetFnColortemp(void);
int GetFnContrast(void);
int GetFnSaturation(void);
int GetFnExposure(void);
int GetFnVolume(void);
int GetFnDpadDisabled(void);
int GetFnDpadJoystick(void);
int GetFnTurboA(void);
int GetFnTurboB(void);
int GetFnTurboX(void);
int GetFnTurboY(void);
int GetFnTurboL1(void);
int GetFnTurboL2(void);
int GetFnTurboR1(void);
int GetFnTurboR2(void);

void SetFnBrightness(int);
void SetFnColortemp(int);
void SetFnContrast(int);
void SetFnSaturation(int);
void SetFnExposure(int);
void SetFnVolume(int);
void SetFnDpadDisabled(int);
void SetFnDpadJoystick(int);
void SetFnTurboA(int);
void SetFnTurboB(int);
void SetFnTurboX(int);
void SetFnTurboY(int);
void SetFnTurboL1(int);
void SetFnTurboL2(int);
void SetFnTurboR1(int);
void SetFnTurboR2(int);

#endif // __msettings_h__
