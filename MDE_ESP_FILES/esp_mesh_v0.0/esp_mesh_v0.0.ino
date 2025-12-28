#include <"esp_mesh.h">

void setup() {
  // put your setup code here, to run once:
  /*  mesh initialization */
  ESP_ERROR_CHECK(esp_mesh_init());
  /*  register mesh events handler */
  ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
}

void loop() {
  // put your main code here, to run repeatedly:

}
