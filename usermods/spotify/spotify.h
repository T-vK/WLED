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
    
    JsonDocument playbackState;
    JsonDocument audioAnalysis;

    response playbackStateResponse;
    response audioAnalysisResponse;

    JsonDocument playbackStateFilter;    
    JsonDocument audioAnalysisFilter;

    String redirectUriPath = "/spotify-callback";
    String authCode = "";
    String redirectUri = "";

    static const char _name[];
    static const char _enabled[];

  public:

    inline void enable(bool enable) { enabled = enable; }

    inline bool isEnabled() { return enabled; }

    void connected() override {
      DEBUG_PRINTLN(F("Connected to WiFi!"));
      DEBUG_PRINT(F("IP Address: "));
      DEBUG_PRINTLN(WiFi.localIP());
      redirectUri = "http://" + WiFi.localIP().toString() + redirectUriPath;
    }

    void setup() {
      playbackStateFilter["timestamp"] = true;
      playbackStateFilter["is_playing"] = true;
      playbackStateFilter["progress_ms"] = true;
      playbackStateFilter["item"]["id"] = true;
      playbackStateFilter["item"]["name"] = true;
      
      audioAnalysisFilter["track"]["tempo"] = true;
      audioAnalysisFilter["track"]["time_signature"] = true;
      audioAnalysisFilter["track"]["key"] = true;
      audioAnalysisFilter["track"]["mode"] = true;
      audioAnalysisFilter["beats"][0]["start"] = true;
      audioAnalysisFilter["bars"][0]["start"] = true;
      
      um_data = new um_data_t;
      um_data->u_size = 2;
      um_data->u_type = new um_types_t[um_data->u_size];
      um_data->u_data = new void*[um_data->u_size];
      um_data->u_data[0] = &playbackState;
      um_data->u_type[0] = UMT_FLOAT; // FIXME
      um_data->u_data[1] = &audioAnalysis;
      um_data->u_type[1] = UMT_UINT16; // FIXME

      server.on(redirectUriPath, HTTP_GET, [this](AsyncWebServerRequest *request){
        DEBUG_PRINTLN(F("callback was called!"));
        if (request->hasParam("code")) {
          redirectUri = "http://" + request->host() + redirectUriPath;
          DEBUG_PRINT(F("Calculated Redirect URI: "));
          DEBUG_PRINTLN(redirectUri);
          AsyncWebParameter* param = request->getParam("code");
          authCode = param->value();
          DEBUG_PRINT(F("Received Auth Code: "));
          DEBUG_PRINTLN(authCode);
          if (requestRefreshToken()) {
            if (sp->get_access_token()) {
              DEBUG_PRINTLN(F("Got access token!"));
            } else {
              DEBUG_PRINTLN(F("Failed to get access token!"));
            }
          }
          DEBUG_PRINTLN(F("Redirecting to /settings/um"));
          request->redirect("/settings/um");
        } else {
          request->send(400, "text/plain", "Missing 'code' parameter");
        }
      });

      initDone = true;
    }

    bool requestRefreshToken() {
      if (sp) {
        DEBUG_PRINT("Auth Code: ");
        DEBUG_PRINTLN(authCode);
        DEBUG_PRINT("Redirect URI: ");
        DEBUG_PRINTLN(redirectUri);
        bool gotToken = sp->get_refresh_token(authCode.c_str(), redirectUri.c_str());
        if (gotToken) {
          refreshToken = sp->get_user_tokens().refresh_token;
          DEBUG_PRINT(F("Received Refresh Token: "));
          DEBUG_PRINTLN(refreshToken);
          if (refreshToken != "") {
            authCode = "";
            serializeConfig();
          }
        } else {
          DEBUG_PRINTLN(F("Failed to get refresh token!"));
        }
        return gotToken;
      } else {
        DEBUG_PRINTLN(F("Spotify object not initialized! Likely because clientId and/or clientSecret are missing!"));
        return false;
      }
    }

    void loop() {
      if (!enabled || strip.isUpdating()) return;

      if (!sp && clientId != "" && clientSecret != "") {
        DEBUG_PRINT(F("clientId: "));
        DEBUG_PRINTLN(clientId);
        DEBUG_PRINT(F("clientSecret: "));
        DEBUG_PRINTLN(clientSecret);
        if (refreshToken != "") {
          sp = new Spotify(clientId.c_str(), clientSecret.c_str(), refreshToken.c_str(), 80);
        } else {
          sp = new Spotify(clientId.c_str(), clientSecret.c_str(), 80);
        }
        DEBUG_PRINTLN(F("Instanciated Spotify object"));
        sp->begin();
        DEBUG_PRINTLN(F("Called Spoitfy::begin()"));
      }

      if (millis() - lastTime > apiQueryInterval && sp && WiFi.status() == WL_CONNECTED) {
        lastTime = millis();
        if (redirectUri != "" && clientId != "" && clientSecret != "") {
          if (authCode != "") {
            if (requestRefreshToken()) {
              if (sp->get_access_token()) {
                DEBUG_PRINTLN(F("Got access token!"));
              } else {
                DEBUG_PRINTLN(F("Failed to get access token!"));
              }
            } else {
              return;
            }
          } else if (refreshToken == "") {
            DEBUG_PRINTLN(F("No auth code or refresh token available!"));
            return;
          }
        } else {
          DEBUG_PRINTLN(F("Missing clientId, clientSecret and/or redirectUri!"));
          return;
        }
        //sp->get_token(); // Todo
        //DEBUG_PRINTLN("Got access token!");

        const String previousTrackId = playbackState["item"]["id"].as<String>();
        playbackStateResponse = sp->current_playback_state(playbackStateFilter);
        int statusCode = playbackStateResponse.status_code;
        DEBUG_PRINT("Status Code: ");
        DEBUG_PRINTLN(statusCode);
        playbackState = playbackStateResponse.reply;
        const String trackId = playbackState["item"]["id"].as<String>();

        if (previousTrackId == trackId) {
          return; // No need to update if the track is the same
        }

        if (trackId == "null") {
          DEBUG_PRINTLN("No song is currently playing!");
          return;
        }

        String songTitle = playbackState["item"]["name"].as<String>();
        int progressMs = playbackState["progress_ms"].as<int>();
        String playbackPosition = formatMilliseconds(progressMs);

        DEBUG_PRINT("Song Title: ");
        DEBUG_PRINTLN(songTitle);
        DEBUG_PRINT("Song ID: ");
        DEBUG_PRINTLN(trackId);
        DEBUG_PRINT("Playback Position: ");
        DEBUG_PRINTLN(playbackPosition);
        
        DEBUG_PRINTLN("Getting audio analysis...");
        audioAnalysisResponse = sp->get_track_audio_analysis(trackId.c_str(), audioAnalysisFilter);
        audioAnalysis = audioAnalysisResponse.reply;

        float tempo = audioAnalysis["track"]["tempo"].as<float>();
        int key = audioAnalysis["track"]["key"].as<int>();
        int mode = audioAnalysis["track"]["mode"].as<int>();
        String keySignature = getKeySignature(key, mode);
        int timeSignatureNumerator = audioAnalysis["track"]["time_signature"].as<int>();
        String timeSignature = String(timeSignatureNumerator) + "/4";

        DEBUG_PRINT("Tempo: ");
        DEBUG_PRINT(tempo);
        DEBUG_PRINTLN("bpm");
        DEBUG_PRINT("Time Signature: ");
        DEBUG_PRINTLN(timeSignature);
        DEBUG_PRINT("Key Signature: ");
        DEBUG_PRINTLN(keySignature);
        
        JsonArray bars = audioAnalysis["bars"].as<JsonArray>();
        for (JsonVariant bar : bars) {
            float barStartTime = bar["start"].as<float>();
            int barStartTimeMs = (float)barStartTime*1000.0;
            if (barStartTimeMs >= progressMs) {
              DEBUG_PRINT("Next bar in: ");
              DEBUG_PRINT(barStartTimeMs-progressMs);
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

};

const char SpotifyUsermod::_name[]    PROGMEM = "SpotifyUsermod";
const char SpotifyUsermod::_enabled[] PROGMEM = "enabled";