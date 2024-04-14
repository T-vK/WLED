#pragma once

#include "wled.h"

// #define DISABLE_PLAYER
#define DISABLE_ALBUM
#define DISABLE_ARTIST
#define DISABLE_AUDIOBOOKS
#define DISABLE_CATEGORIES
#define DISABLE_CHAPTERS
#define DISABLE_EPISODES
#define DISABLE_GENRES
#define DISABLE_MARKETS
#define DISABLE_PLAYLISTS
#define DISABLE_SEARCH
#define DISABLE_SHOWS
// #define DISABLE_TRACKS
#define DISABLE_USER
#define DISABLE_SIMPIFIED
#define DISABLE_WEB_SERVER
#include <SpotifyEsp32.h>
#include <SpotifyEsp32.cpp>

const char *renewRefreshTokenJs PROGMEM =
  "window.getCfg = async () => (await (await fetch(getURL(`/cfg.json`))).json()).um.SpotifyUsermod;"
  "window.redirectUri = `${window.location.origin}/spotify-callback`;"
  "window.getAuthUrl = (clientId) => `https://accounts.spotify.com/authorize?response_type=code&client_id=${clientId}&redirect_uri=${encodeURIComponent(window.redirectUri)}&scope=user-read-playback-state`;"
  "window.renewRefreshToken = async () => {"
    "const {clientId, clientSecret} = await getCfg();"
    "if (clientId && clientSecret) {"
      "window.location.href = await window.getAuthUrl(clientId);"
    "} else {"
      "alert(`Error: Please enter ClientSecret and ClientId and save!`);"
    "}"
  "};";

// const char* redirectUriButtonHtml PROGMEM = "<input type=\"button\" onclick=\"this.value=`${window.location.origin}/spotify-callback`;this.type=`text`\" value=\"Show\">";
// const char* renewRefreshTokenLinkHtml PROGMEM = "<a href=\"#\" onclick=\"renewRefreshToken().catch(alert); return false;\">Get new refresh token</a>";

class SpotifyUsermod : public Usermod {

  private:

    bool enabled = true;
    bool initDone = false;
    unsigned long lastTime = 0;

    Spotify* sp;
    String clientId = "";
    String clientSecret = "";
    String refreshToken = "";
    int audioDelay = 10; // in milliseconds
    unsigned long apiQueryInterval = 1000; // in milliseconds

    static const char _name[];
    static const char _enabled[];

  public:

    inline void enable(bool enable) { enabled = enable; }

    inline bool isEnabled() { return enabled; }

    void connected() override {
      DEBUG_PRINTLN(F("Connected to WiFi!"));
      DEBUG_PRINT(F("IP Address: "));
      DEBUG_PRINTLN(WiFi.localIP());
    }

    void setup() {
      server.on("/spotify-callback", HTTP_GET, [this](AsyncWebServerRequest *request){
        DEBUG_PRINTLN(F("/spotify-callback was called!"));
        if (request->hasParam("code")) {
          AsyncWebParameter* param = request->getParam("code");
          const char * const authCode = param->value().c_str();
          DEBUG_PRINT(F("Auth Code: "));
          DEBUG_PRINTLN(authCode);
          const char* const host = request->host().c_str();
          DEBUG_PRINT(F("Host: "));
          DEBUG_PRINTLN(host);
          const char* const protocol = "http";
          DEBUG_PRINT(F("Protocol: "));
          DEBUG_PRINTLN(protocol);
          const char* const redirectUriRelative = request->url().c_str(); // "/spotify-";
          DEBUG_PRINT(F("Redirect URI Relative: "));
          DEBUG_PRINTLN(redirectUriRelative);
          const char* const redirectUri = (String(protocol) + "://" + String(host) + String(redirectUriRelative)).c_str();
          DEBUG_PRINT(F("Redirect URI: "));
          DEBUG_PRINTLN(redirectUri);
          if (sp) {
            sp->get_refresh_token(authCode, redirectUri);
            refreshToken = sp->get_user_tokens().refresh_token;
            DEBUG_PRINT(F("New refreshToken: "));
            DEBUG_PRINTLN(refreshToken);
            serializeConfig();
          } else {
            DEBUG_PRINTLN(F("Spotify object not initialized! Likely because clientId and/or clientSecret are missing!"));
          }
          DEBUG_PRINTLN(F("Redirecting to /settings/um"));
          request->redirect("/settings/um");
        } else {
          request->send(400, "text/plain", "Missing 'code' parameter");
        }
      });

      initDone = true;
    }

    String getKeySignature(int key, int mode) {
        if (key == -1 || mode == -1) // No key detected
            return "Unknown";

        String keySignature;
        const String keys[12] = {"C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};

        // Determine key signature based on key and mode
        if (mode == 0) { // Minor
            keySignature.concat(keys[(key + 9) % 12]);
            keySignature.concat(" minor");
        } else if (mode == 1) { // Major
            keySignature.concat(keys[key]);
            keySignature.concat(" major");
        }

        return keySignature;
    }

    String formatMilliseconds(int milliseconds) {
        int seconds = milliseconds / 1000; // Convert milliseconds to seconds
        int minutes = seconds / 60; // Extract minutes
        seconds %= 60; // Extract remaining seconds

        String formattedTime;
        if (minutes < 10) {
            formattedTime.concat("0"); // Add leading zero for minutes < 10
        }
        formattedTime.concat(String(minutes) + ":");
        if (seconds < 10) {
            formattedTime.concat("0"); // Add leading zero for seconds < 10
        }
        formattedTime.concat(String(seconds));
        
        return formattedTime;
    }

    void loop() {
      if (!enabled || strip.isUpdating()) return;

      if (!sp && clientId != "" && clientSecret != "") {
        DEBUG_PRINT(F("clientId: "));
        DEBUG_PRINTLN(clientId);
        DEBUG_PRINT(F("clientSecret: "));
        DEBUG_PRINTLN(clientSecret);
        if (refreshToken != "") {
          sp = new Spotify(clientId.c_str(), clientSecret.c_str(), refreshToken.c_str(), 80, true);
        } else {
          sp = new Spotify(clientId.c_str(), clientSecret.c_str(), 80, true);
        }
        DEBUG_PRINTLN(F("Instanciated Spotify object"));
        sp->begin();
        DEBUG_PRINTLN(F("Called Spoitfy::begin()"));
      }

      if (millis() - lastTime > 8000 && sp && sp->is_auth() && WiFi.status() == WL_CONNECTED) {
        //sp->get_token(); // Todo
        //DEBUG_PRINTLN("Got access token!");
        lastTime = millis();
        JsonDocument playback_state_filter;
        playback_state_filter["timestamp"] = true;
        playback_state_filter["is_playing"] = true;
        playback_state_filter["progress_ms"] = true;
        playback_state_filter["item"]["id"] = true;
        playback_state_filter["item"]["name"] = true;
        
        JsonDocument audio_analysis_filter;
        audio_analysis_filter["track"]["tempo"] = true;
        audio_analysis_filter["track"]["time_signature"] = true;
        audio_analysis_filter["track"]["key"] = true;
        audio_analysis_filter["track"]["mode"] = true;
        audio_analysis_filter["beats"][0]["start"] = true;
        audio_analysis_filter["bars"][0]["start"] = true;

        response playback_state_response = sp->current_playback_state(playback_state_filter);
        int status_code = playback_state_response.status_code;
        const String track_id = playback_state_response.reply["item"]["id"].as<String>();

        DEBUG_PRINT("Status Code: ");
        DEBUG_PRINTLN(status_code);

        if (track_id == "null") {
          DEBUG_PRINTLN("No song is currently playing!");
          return;
        }

        String song_title = playback_state_response.reply["item"]["name"].as<String>();
        //String song_id = playback_state_response.reply["item"]["id"].as<String>();

        int progress_ms = playback_state_response.reply["progress_ms"].as<int>();
        String playback_position = formatMilliseconds(progress_ms);

        DEBUG_PRINT("Song Title: ");
        DEBUG_PRINTLN(song_title);
        DEBUG_PRINT("Song ID: ");
        DEBUG_PRINTLN(track_id);
        DEBUG_PRINT("Playback Position: ");
        DEBUG_PRINT(playback_position);


        DEBUG_PRINTLN("\nGetting audio analysis...");
        response audio_analysis = sp->get_track_audio_analysis(track_id.c_str(), audio_analysis_filter);

        float tempo = audio_analysis.reply["track"]["tempo"].as<float>();
        int key = audio_analysis.reply["track"]["key"].as<int>();
        int mode = audio_analysis.reply["track"]["mode"].as<int>();
        String key_signature = getKeySignature(key, mode);
        int time_signature_numerator = audio_analysis.reply["track"]["time_signature"].as<int>();
        String time_signature = String(time_signature_numerator) + "/4";

        DEBUG_PRINT("Tempo: ");
        DEBUG_PRINT(tempo);
        DEBUG_PRINTLN("bpm");
        DEBUG_PRINT("Time Signature: ");
        DEBUG_PRINTLN(time_signature);
        DEBUG_PRINT("Key Signature: ");
        DEBUG_PRINTLN(key_signature);
        
        JsonArray bars = audio_analysis.reply["bars"].as<JsonArray>();
        for (JsonVariant bar : bars) {
            float bar_start_time = bar["start"].as<float>();
            int bar_start_time_ms = (float)bar_start_time*1000.0;
            if (bar_start_time_ms >= progress_ms) {
              DEBUG_PRINT("Next bar in: ");
              DEBUG_PRINT(bar_start_time_ms-progress_ms);
              DEBUG_PRINTLN("ms");
              break;
            }
        }
      }
    }

    void addToJsonInfo(JsonObject& root) {
      // if "u" object does not exist yet wee need to create it
      JsonObject user = root["u"];
      if (user.isNull()) user = root.createNestedObject("u");
    }

    void addToJsonState(JsonObject& root) {
      if (!initDone || !enabled) return;  // prevent crash on boot applyPreset()

      JsonObject usermod = root[FPSTR(_name)];
      if (usermod.isNull()) usermod = root.createNestedObject(FPSTR(_name));

      //usermod["user0"] = userVar0;
    }

    void readFromJsonState(JsonObject& root) {
      if (!initDone) return;  // prevent crash on boot applyPreset()

      //JsonObject usermod = root[FPSTR(_name)];
      //if (!usermod.isNull()) {
        // expect JSON usermod data in usermod name object: {"SpotifyUsermod:{"user0":10}"}
      //  userVar0 = usermod["user0"] | userVar0; //if "user0" key exists in JSON, update, else keep old value
      //}
    }

    void addToConfig(JsonObject& root) {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top[FPSTR(_enabled)] = enabled;
      //save these vars persistently whenever settings are saved
      top["clientId"] = clientId;
      top["clientSecret"] = clientSecret;
      top["refreshToken"] = refreshToken;
      top["audioDelay"] = audioDelay;
      top["apiQueryInterval"] = apiQueryInterval;
    }

    bool readFromConfig(JsonObject& root) {
      JsonObject top = root[FPSTR(_name)];

      bool configComplete = !top.isNull();

      configComplete &= getJsonValue(top["clientId"], clientId);
      configComplete &= getJsonValue(top["clientSecret"], clientSecret);
      configComplete &= getJsonValue(top["refreshToken"], refreshToken);
      configComplete &= getJsonValue(top["audioDelay"], audioDelay);
      configComplete &= getJsonValue(top["apiQueryInterval"], apiQueryInterval);

      return configComplete;
    }

    void appendConfigData() {
      oappend(renewRefreshTokenJs);
      oappend(SET_F("addInfo('")); oappend(String(FPSTR(_name)).c_str()); oappend(SET_F(":clientId")); oappend(SET_F("',1,'');"));
      oappend(SET_F("addInfo('")); oappend(String(FPSTR(_name)).c_str()); oappend(SET_F(":clientSecret")); oappend(SET_F("',1,'</br>Redirect URL <input type=\"button\" onclick=\"this.value=window.redirectUri;this.type=`text`\" value=\"Show\"><i></i></br><i>(Get Client ID and Client Secret using the Create App button as <a href=\"https://developer.spotify.com/documentation/web-api/concepts/apps\">described here</a></i></br>');"));
      oappend(SET_F("addInfo('")); oappend(String(FPSTR(_name)).c_str()); oappend(SET_F(":refreshToken")); oappend(SET_F("',1,'<a href=\"#\" onclick=\"window.renewRefreshToken().catch(alert);return false;\">Get new refresh token</a>');"));
      oappend(SET_F("addInfo('")); oappend(String(FPSTR(_name)).c_str()); oappend(SET_F(":audioDelay")); oappend(SET_F("',1,'milliseconds <i></br>(Enter the delay between your Spotify player and sound coming out of the speakers)</i></br>');"));
      oappend(SET_F("addInfo('")); oappend(String(FPSTR(_name)).c_str()); oappend(SET_F(":apiQueryInterval")); oappend(SET_F("',1,'milliseconds <i></br>(This is also the time it will take for WLED to react to song changes. Too low values will get you rate-limited.)</i></br>');"));
    }

    uint16_t getId() {
      return USERMOD_ID_SPOTIFY;
    }

};

const char SpotifyUsermod::_name[]    PROGMEM = "SpotifyUsermod";
const char SpotifyUsermod::_enabled[] PROGMEM = "enabled";