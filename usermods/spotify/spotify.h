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
#include <ESPNtpClient.h>

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

struct MusicalElement {
    int previous = 0;
    int current = 0;
    int next = 0;
    int currentIndex = 0;
    bool isNew = false;
};

// const char* redirectUriButtonHtml PROGMEM = "<input type=\"button\" onclick=\"this.value=`${window.location.origin}/spotify-callback`;this.type=`text`\" value=\"Show\">";
// const char* renewRefreshTokenLinkHtml PROGMEM = "<a href=\"#\" onclick=\"renewRefreshToken().catch(alert); return false;\">Get new refresh token</a>";

class SpotifyUsermod : public Usermod {

  private:

    bool enabled = true;
    bool initDone = false;
    int64_t lastTime = 0;

    Spotify* sp;
    String clientId = "";
    String clientSecret = "";
    String refreshToken = "";
    int audioDelay = 10; // in milliseconds
    int64_t apiQueryInterval = 1000; // in milliseconds
    
    JsonDocument playbackState;
    JsonDocument audioAnalysis;

    response playbackStateResponse;
    response audioAnalysisResponse;

    JsonDocument playbackStateFilter;    
    JsonDocument audioAnalysisFilter;

    MusicalElement bars;
    MusicalElement beats;
    // MusicalElement tatums;
    // MusicalElement segments;
    // MusicalElement sections;

    int64_t _serverProgressMs = 0; // raw progress as received from Spotify
    int64_t _serverTime = 0; // raw timestamp as received from Spotify

    float _beatDetectionMaxFps = 0.3;
    int _beatDetectionInterval = 1000/_beatDetectionMaxFps;
    int _lastBeatDetectionTime = 0;

    int beatCounter = 0;
    int timeSignatureNumerator = 0; // 4 = 4/4; 3 = 3/4; etc - The denominator is always 4

    int64_t ntpTimeRelative = 0; // ntpTimeRelative+millis() = unix time
    bool ntpSynced = false;
    int64_t progressMs = 0; // progress in milliseconds
    int64_t previousProgressMs = 0; // progress in milliseconds
    int64_t now = 0; // current time in milliseconds
    int64_t previousNow = 0; // previous loop time in milliseconds

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
      playbackStateFilter["message"] = true; // in case of errors
      playbackStateFilter["timestamp"] = true;
      playbackStateFilter["is_playing"] = true;
      playbackStateFilter["progress_ms"] = true;
      playbackStateFilter["item"]["id"] = true;
      playbackStateFilter["item"]["name"] = true;
      playbackStateFilter["item"]["artists"][0]["name"] = true;
      playbackStateFilter["item"]["duration_ms"] = true;
      playbackStateFilter["item"]["type"] = true;
      
      audioAnalysisFilter["message"] = true; // in case of errors
      audioAnalysisFilter["track"]["tempo"] = true;
      audioAnalysisFilter["track"]["time_signature"] = true;
      audioAnalysisFilter["track"]["key"] = true;
      audioAnalysisFilter["track"]["mode"] = true;
      audioAnalysisFilter["beats"][0]["start"] = true;
      audioAnalysisFilter["bars"][0]["start"] = true;
      
      // um_data = new um_data_t;
      // um_data->u_size = 2;
      // um_data->u_type = new um_types_t[um_data->u_size];
      // um_data->u_data = new void*[um_data->u_size];
      // um_data->u_data[0] = &playbackState;
      // um_data->u_type[0] = UMT_FLOAT; // FIXME
      // um_data->u_data[1] = &audioAnalysis;
      // um_data->u_type[1] = UMT_UINT16; // FIXME


      NTP.onNTPSyncEvent([this] (NTPEvent_t event) {
        if (event.event == timeSyncd) {
          ntpSynced = true;
          ntpSync();
          DEBUG_PRINT(F("NTP time synched! Time: "));
          DEBUG_PRINTLN(ntpTimeRelative);
        }
      });
      DEBUG_PRINTLN("Asynchronously synching time with NTP...");
      NTP.begin("pool.ntp.org");

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

    void ntpSync() { // TODO: Check if there are any events we need to handle when the time is updated
      int64_t ntpTimeMs = NTP.micros()/1000;
      int64_t localTimeMs = millis();
      //ntpSynced = ntpTimeMs > 1000000000000;
      //if (ntpSynced) {
        ntpTimeRelative = ntpTimeMs-localTimeMs;
      //}
    }

    void updateTime() {
      now = ntpTimeRelative+millis();
    }

    void updateProgressMs() {
        progressMs = _serverProgressMs + (now - _serverTime);
    }

    void loop() {
      if (!enabled || strip.isUpdating()) return;

      if (!sp && clientId != "" && clientSecret != "") {
        DEBUG_PRINT(F("clientId: "));
        DEBUG_PRINTLN(clientId);
        DEBUG_PRINT(F("clientSecret: "));
        DEBUG_PRINTLN(clientSecret);
        if (refreshToken != "") {
          sp = new Spotify(clientId.c_str(), clientSecret.c_str(), refreshToken.c_str(), 80, false);
        } else {
          sp = new Spotify(clientId.c_str(), clientSecret.c_str(), 80, false);
        }
        DEBUG_PRINTLN(F("Instanciated Spotify object"));
        sp->begin();
        DEBUG_PRINTLN(F("Called Spoitfy::begin()"));
      }

      bool isConnected = WiFi.status() == WL_CONNECTED;
       // TODO: ntpSync always needs like 20 tries before it works. We might just have to call it once and then give it some time
      /*if (isConnected && ntpTimeRelative < 1713775217311) {
        ntpSync();
        if (ntpTimeRelative > 1713775217311) {
          DEBUG_PRINT(F("NTP time: "));
          DEBUG_PRINTLN(ntpTimeRelative);
        } else {
          DEBUG_PRINTLN(F("Failed to sync time with NTP server!"));
          return;
        }
      }*/
      if (!ntpSynced && !isConnected) {
        return;
      }
      // if (isConnected && !ntpSynced) {
      //   ntpSync();
      //   if (ntpSynced) {
      //     DEBUG_PRINT(F("NTP time: "));
      //     DEBUG_PRINTLN(ntpTimeRelative);
      //   } else {
      //     DEBUG_PRINTLN(F("Failed to sync time with NTP server!"));
      //     return;
      //   }
      // }

      if (isConnected && sp && millis() - lastTime > apiQueryInterval) {
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

        if (progressMs > 0) {
          DEBUG_PRINT("Progress: ");
          DEBUG_PRINTLN(progressMs);
          DEBUG_PRINT("Beat Info: ");
          DEBUG_PRINT(beatCounter);
          DEBUG_PRINT("/4 - index:");
          DEBUG_PRINT(beats.currentIndex);
          DEBUG_PRINT("- ");
          DEBUG_PRINT(beats.current);
          DEBUG_PRINTLN("ms");
          DEBUG_PRINT("Bar Info: ");
          DEBUG_PRINT(bars.currentIndex);
          DEBUG_PRINT("- ");
          DEBUG_PRINT(bars.current);
          DEBUG_PRINTLN("ms");
        } else {
          DEBUG_PRINTLN("Progress is 0!");
        }

        const String previousTrackId = playbackState["item"]["id"].as<String>();
        DEBUG_PRINTLN("Requesting playback state from Spotify...");
        int beforeRequest = millis();
        playbackStateResponse = sp->current_playback_state(playbackStateFilter);
        DEBUG_PRINT("Requesting playback state took ");
        DEBUG_PRINT(millis()-beforeRequest);
        DEBUG_PRINTLN("ms.");
        int statusCode = playbackStateResponse.status_code;
        DEBUG_PRINT("Status Code: ");
        DEBUG_PRINTLN(statusCode);
        if (statusCode < 200 || statusCode >= 400) {
          DEBUG_PRINTLN("Failed to get playback state! - Response: ");
          serializeJson(playbackStateResponse.reply, Serial);
          DEBUG_PRINTLN("");
          return;
        }
        playbackState = playbackStateResponse.reply;
        const String type = playbackState["item"]["type"].as<String>();
        const String trackId = playbackState["item"]["id"].as<String>();

        if (trackId == "null" || type != "track") {
          DEBUG_PRINTLN("No song is currently playing! - Response: ");
          serializeJson(playbackState, Serial);
          DEBUG_PRINTLN("");
          return;
        }

        _serverProgressMs = playbackState["progress_ms"].as<int>();
        _serverTime = playbackState["timestamp"].as<int64_t>();
        DEBUG_PRINT("Server time: ");
        DEBUG_PRINTLN(_serverTime);
        
        if (previousTrackId != trackId) { // track changed
          DEBUG_PRINTLN("Requesting audio analysis from Spotify...");
          int beforeRequest = millis();
          audioAnalysisResponse = sp->get_track_audio_analysis(trackId.c_str(), audioAnalysisFilter);
          DEBUG_PRINT("Requesting audio analysis took ");
          DEBUG_PRINT(millis()-beforeRequest);
          DEBUG_PRINTLN("ms.");
          if (statusCode == -1 || statusCode >= 400) {
            DEBUG_PRINTLN("Failed to get audio analysis! - Response: ");
            serializeJson(audioAnalysisResponse.reply, Serial);
            playbackState["item"]["id"] = "null"; // reset trackId
            return;
          }
          audioAnalysis = audioAnalysisResponse.reply;
          timeSignatureNumerator = audioAnalysis["track"]["time_signature"].as<int>();
          printDebugInfo();
        }
        
        // JsonArray bars = audioAnalysis["bars"].as<JsonArray>();
        // for (JsonVariant bar : bars) {
        //   float barStartTime = bar["start"].as<float>();
        //   int barStartTimeMs = (float)barStartTime*1000.0;
        //   if (barStartTimeMs >= progressMs) {
        //     DEBUG_PRINT("Next bar in: ");
        //     DEBUG_PRINT(barStartTimeMs-progressMs);
        //     DEBUG_PRINTLN("ms");
        //     break;
        //   }
        // }
        if (_serverTime == 0) {
          DEBUG_PRINTLN("Not synced with Spotify yet!");
        }
        if (!ntpSynced) {
          DEBUG_PRINTLN("Not synced with NTP yet!");
        }
      }
      if (_serverTime == 0 || !ntpSynced) {
        return;
      }

      updateTime();
      updateProgressMs();

      updateMusicalElementTimes("bars");
      updateMusicalElementTimes("beats");

      updateTime();
      updateProgressMs();

      //if (_serverTime > 0 && _serverProgressMs > 0 && millis() - _lastBeatDetectionTime > _beatDetectionInterval) {
        // _lastBeatDetectionTime = millis();
        // DEBUG_PRINT("Local time: ");
        // DEBUG_PRINTLN(now);
        // DEBUG_PRINT("Progress: ");
        // DEBUG_PRINT(progressMs/1000);
        // DEBUG_PRINTLN("s");
        // updateMusicalElementTimes("bars");
        // updateMusicalElementTimes("beats");
        // updateTime();
        // DEBUG_PRINT("Next bar in: ");
        // DEBUG_PRINT(bars.next-progressMs);
        // DEBUG_PRINTLN("ms");
        // DEBUG_PRINT("Current bar started ");
        // DEBUG_PRINT(progressMs-bars.current);
        // DEBUG_PRINTLN("ms ago");
        // DEBUG_PRINT("Previous bar started ");
        // DEBUG_PRINT(progressMs-bars.previous);
        // DEBUG_PRINTLN("ms ago");

        // DEBUG_PRINT("Next beat in: ");
        // DEBUG_PRINT(beats.next-progressMs);
        // DEBUG_PRINTLN("ms");
        // DEBUG_PRINT("Current beat started ");
        // DEBUG_PRINT(progressMs-beats.current);
        // DEBUG_PRINTLN("ms ago");
        // DEBUG_PRINT("Previous beat started ");
        // DEBUG_PRINT(progressMs-beats.previous);
        // DEBUG_PRINTLN("ms ago");
      //}

      // DEBUG_PRINT("Progress: ");
      // DEBUG_PRINT(progressMs);
      // DEBUG_PRINTLN("ms");
      // DEBUG_PRINT("Previous progress: ");
      // DEBUG_PRINT(previousProgressMs);
      // DEBUG_PRINTLN("ms");
      // DEBUG_PRINT("Current Bar Time: ");
      // DEBUG_PRINTLN(bars.current);


      // if new bar detected
      if (bars.isNew && progressMs > 0) {
        beatCounter = 0;
        DEBUG_PRINT("New bar detected! Index: ");
        DEBUG_PRINT(bars.currentIndex);
        DEBUG_PRINT(" - ");
        DEBUG_PRINT(bars.current);
        DEBUG_PRINTLN("ms");
        DEBUG_PRINT("Playback Position: ");
        DEBUG_PRINT(progressMs/1000);
        DEBUG_PRINT("s - ");
        DEBUG_PRINT(progressMs);
        DEBUG_PRINTLN("ms");
        // DEBUG_PRINT("Distance to previous beat: ");
        // DEBUG_PRINTLN(beats.previous-bars.current);
        // DEBUG_PRINT("Distance to current beat: ");
        // DEBUG_PRINTLN(bars.current-beats.current);
        // DEBUG_PRINT("Distance to next beat: ");
        // DEBUG_PRINTLN(beats.next-bars.current);
        bars.isNew = false;
      }
      // if new beat detected
      if (beats.isNew && progressMs > 0) {
        beatCounter++;
        DEBUG_PRINT("New beat detected: ");
        DEBUG_PRINT(beatCounter);
        DEBUG_PRINTLN("/4");
        beats.isNew = false;
      }
      // timeSignatureNumerator

      previousNow = now;
      previousProgressMs = progressMs;
    }

    void printDebugInfo() {
        String trackId = playbackState["item"]["id"].as<String>();
        String trackName = playbackState["item"]["name"].as<String>();
        String artists = "";
        JsonArray artistsArr = playbackState["item"]["artists"].as<JsonArray>();
        for (JsonVariant artist : artistsArr) {
          artists.concat(artist["name"].as<String>());
          artists.concat(" & ");
        }
        bool isPlaying = playbackState["is_playing"].as<bool>();
        int progressMs = playbackState["progress_ms"].as<int>();
        int durationMs = playbackState["item"]["duration_ms"].as<int>();
        //String playbackPosition = formatMilliseconds(progressMs);

        if (isPlaying) {
          DEBUG_PRINT("[PLAYING]: ");
        } else {
          DEBUG_PRINT("[PAUSED]: ");
        }
        DEBUG_PRINT(artists + " - " + trackName);
        DEBUG_PRINT(" | Track ID: ");
        DEBUG_PRINTLN(trackId);
        DEBUG_PRINT("Playback Position: ");
        DEBUG_PRINT(progressMs/1000);
        DEBUG_PRINT("s / ");
        DEBUG_PRINT(durationMs/1000);
        DEBUG_PRINTLN("s");

        float tempo = audioAnalysis["track"]["tempo"].as<float>();
        // int key = audioAnalysis["track"]["key"].as<int>();
        // int mode = audioAnalysis["track"]["mode"].as<int>();
        // String keySignature = getKeySignature(key, mode);
        int timeSignatureNumerator = audioAnalysis["track"]["time_signature"].as<int>();
        String timeSignature = String(timeSignatureNumerator) + "/4";

        DEBUG_PRINT("Tempo: ");
        DEBUG_PRINT(tempo);
        DEBUG_PRINTLN("bpm");
        DEBUG_PRINT("Time Signature: ");
        DEBUG_PRINTLN(timeSignature);
        // DEBUG_PRINT("Key Signature: ");
        // DEBUG_PRINTLN(keySignature);

        // MusicalElement barsTimes = updateMusicalElementTimes("bars");
        // int nextBarStart = barsTimes.next;
        // DEBUG_PRINT("Next bar in: ");
        // DEBUG_PRINT((nextBarStart-progressMs)/1000);
        // DEBUG_PRINTLN("s");

        DEBUG_PRINT("Server time: ");
        DEBUG_PRINTLN(_serverTime);
        DEBUG_PRINT("Server progress: ");
        DEBUG_PRINTLN(_serverProgressMs);
    }

    void updateMusicalElementTimes(String elementType) { // elementType = "bars" or "beats" or "tatums" or "segments" or "sections"
      MusicalElement elementTimes;

      if (elementType == "bars") {
        elementTimes = bars;
      } else if (elementType == "beats") {
        elementTimes = beats;
      }

      int startIndex;
      if (progressMs < elementTimes.current || elementTimes.currentIndex == 0) {
        startIndex = 0;
      } else if (progressMs >= elementTimes.next) {
        startIndex = elementTimes.currentIndex;
      } else if (progressMs < elementTimes.next) {
        return;
      }

      JsonArray elements = audioAnalysis[elementType].as<JsonArray>();
      MusicalElement newElementTimes;
      for (int i = startIndex; i < elements.size(); i++) {
        JsonVariant element = elements[i];
        float elementStartTime = element["start"].as<float>();
        int elementStartTimeMs = (int)(elementStartTime * 1000.0);
        // TODO: Check if index bigger than max index
        if (elementStartTimeMs > progressMs) {
          newElementTimes.next = elementStartTimeMs;
          newElementTimes.currentIndex = i;
          newElementTimes.isNew = elementTimes.currentIndex != newElementTimes.currentIndex;
          if (!newElementTimes.isNew) {
            return;
          }
          if (i > 0) {
            JsonVariant currentElement = elements[i-1];
            float currentElementStartTime = currentElement["start"].as<float>();
            newElementTimes.current = (int)(currentElementStartTime * 1000.0);
          } else {
            newElementTimes.current = 0;
          }
          if (i > 1) {
            JsonVariant previousElement = elements[i-2];
            float previousElementStartTime = previousElement["start"].as<float>();
            newElementTimes.previous = (int)(previousElementStartTime * 1000.0);
          } else {
            newElementTimes.previous = 0;
          }
          // DEBUG_PRINT("Next: ");
          // DEBUG_PRINTLN(newElementTimes.next);
          // DEBUG_PRINT("Current: ");
          // DEBUG_PRINTLN(newElementTimes.current);
          // DEBUG_PRINT("Previous: ");
          // DEBUG_PRINTLN(newElementTimes.previous);
          // DEBUG_PRINT("progressMs: ");
          // DEBUG_PRINTLN(progressMs);
          break;
        }
      }
      if (elementType == "bars") {
        bars = newElementTimes;
      } else if (elementType == "beats") {
        beats = newElementTimes;
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

    // String getKeySignature(int key, int mode) {
    //     const String keys[12] = {"C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};
    //     if (key == -1 || mode == -1) // No key detected
    //         return "Unknown";
    //
    //     String keySignature = "";
    //
    //     // Determine key signature based on key and mode
    //     if (mode == 0) { // Minor
    //         keySignature.concat(keys[(key + 9) % 12]);
    //         keySignature.concat(" minor");
    //     } else if (mode == 1) { // Major
    //         keySignature.concat(keys[key]);
    //         keySignature.concat(" major");
    //     }
    //
    //     return keySignature;
    // }

    // String formatMilliseconds(int milliseconds) { // converts ms to mm:ss
    //     int seconds = milliseconds / 1000; // Convert milliseconds to seconds
    //     int minutes = seconds / 60; // Extract minutes
    //     seconds %= 60; // Extract remaining seconds
    //
    //     String formattedTime;
    //     if (minutes < 10) {
    //         formattedTime.concat("0"); // Add leading zero for minutes < 10
    //     }
    //     formattedTime.concat(String(minutes) + ":");
    //     if (seconds < 10) {
    //         formattedTime.concat("0"); // Add leading zero for seconds < 10
    //     }
    //     formattedTime.concat(String(seconds));
    //
    //     return formattedTime;
    // }
};

const char SpotifyUsermod::_name[]    PROGMEM = "SpotifyUsermod";
const char SpotifyUsermod::_enabled[] PROGMEM = "enabled";