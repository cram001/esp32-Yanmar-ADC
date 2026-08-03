Import("env")
from SCons.Script import Action
import os


def patch_text_file(path, old, new):
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    if old not in text:
        return False
    text = text.replace(old, new)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    print("Patched", path)
    return True


def patch_sensesp(target, source, env):
    project_dir = env.get("PROJECT_DIR", os.getcwd())
    pioenv = env.get("PIOENV", "")
    if not pioenv:
        print("Warning: PIOENV not set, cannot patch SensESP library.")
        return None

    base_dir = os.path.join(project_dir, ".pio", "libdeps", pioenv,
                            "SensESP", "src", "sensesp", "signalk")

    if not os.path.isdir(base_dir):
        print(f"Warning: SensESP signalk directory not found: {base_dir}")
        return None

    header = os.path.join(base_dir, "signalk_ws_client.h")
    source_cpp = os.path.join(base_dir, "signalk_ws_client.cpp")

    if os.path.isfile(header):
        patch_text_file(
            header,
            "  void connect();\n  void loop();\n  bool is_connected();\n  void restart();\n  void send_delta();\n\n  /**\n",
            "  void connect();\n  void loop();\n  bool is_connected();\n  void restart();\n  void suspend();\n  void resume();\n  bool is_suspended() const;\n  void send_delta();\n\n  /**\n"
        )
        patch_text_file(
            header,
            "  String client_id_ = \"\";\n  String polling_href_ = \"\";\n  String auth_token_ = NULL_AUTH_TOKEN;\n  bool server_detected_ = false;\n  bool token_test_success_ = false;\n\n  TaskQueueProducer<SKWSConnectionState> connection_state_{\n",
            "  String client_id_ = \"\";\n  String polling_href_ = \"\";\n  String auth_token_ = NULL_AUTH_TOKEN;\n  bool server_detected_ = false;\n  bool token_test_success_ = false;\n  bool suspended_ = false;\n\n  TaskQueueProducer<SKWSConnectionState> connection_state_{\n"
        )

    if os.path.isfile(source_cpp):
        patch_text_file(
            source_cpp,
            "void SKWSClient::connect() {\n  if (get_connection_state() != SKWSConnectionState::kSKWSDisconnected) {\n    return;\n  }\n\n  if (!WiFi.isConnected() && WiFi.getMode() != WIFI_MODE_AP) {\n",
            "void SKWSClient::connect() {\n  if (suspended_) {\n    ESP_LOGD(__FILENAME__, \"Websocket client connect is suspended during OTA.\");\n    return;\n  }\n\n  if (get_connection_state() != SKWSConnectionState::kSKWSDisconnected) {\n    return;\n  }\n\n  if (!WiFi.isConnected() && WiFi.getMode() != WIFI_MODE_AP) {\n"
        )
        patch_text_file(
            source_cpp,
            "void SKWSClient::restart() {\n  if (get_connection_state() == SKWSConnectionState::kSKWSConnected) {\n    esp_websocket_client_close(this->client_, portMAX_DELAY);\n    set_connection_state(SKWSConnectionState::kSKWSDisconnected);\n  }\n}\n\nvoid SKWSClient::send_delta() {\n",
            "void SKWSClient::restart() {\n  if (get_connection_state() == SKWSConnectionState::kSKWSConnected) {\n    esp_websocket_client_close(this->client_, portMAX_DELAY);\n    set_connection_state(SKWSConnectionState::kSKWSDisconnected);\n  }\n}\n\nvoid SKWSClient::suspend() {\n  suspended_ = true;\n  if (get_connection_state() == SKWSConnectionState::kSKWSConnected) {\n    esp_websocket_client_close(this->client_, portMAX_DELAY);\n    set_connection_state(SKWSConnectionState::kSKWSDisconnected);\n  }\n}\n\nvoid SKWSClient::resume() {\n  suspended_ = false;\n  if (get_connection_state() == SKWSConnectionState::kSKWSDisconnected) {\n    this->connect();\n  }\n}\n\nbool SKWSClient::is_suspended() const {\n  return suspended_;\n}\n\nvoid SKWSClient::send_delta() {\n"
        )

    return None

env.AddPreAction("buildprog", Action(patch_sensesp, None))
