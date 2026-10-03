# Example configuration for the simulator

On its first start the simulator copies these files to `Platformio/simulator_config/` (or to the folder
set in the environment variable `OMOTE_CONFIG_DIR`) and loads devices, scenes and GUIs from there, the
same way the remote loads them from its flash.

Edit them with the OMOTE Config app: start the simulator, switch on **Settings → Web config**, and set the
remote address in the app to `http://localhost:8081` (another port can be set with `OMOTE_HTTP_PORT`).
**Restart remote** in the app restarts the simulator with the new configuration. To start over with
these examples, delete `simulator_config/`.

The IR codes are only examples. The simulator prints the IR codes it would send, MQTT messages go to the
broker set in `secrets_override.h`.
