#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>
#include <FlashStorage.h>
#include <WiFiNINA.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_Fingerprint finger(&Serial1);

const int MAX_HUELLAS = 10;
const int LONGITUD_NOMBRE = 20;

typedef struct {
  char nombres[MAX_HUELLAS][LONGITUD_NOMBRE];
} DatosEmpleados;

FlashStorage(empleadosStorage, DatosEmpleados);
DatosEmpleados empleados;

// Datos Wi-Fi
char ssid[] = "Ritsa";       // 👉 Cambia por tu red Wi-Fi
char pass[] = "12345678";   // 👉 Cambia por tu contraseña Wi-Fi

WiFiServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -18000, 60000);  // UTC -5 (ajusta tu zona horaria)

String ultimoNombre = "Nadie";
String ultimaHora = "00:00:00";

bool mostrarMensajeUnaVez = true;

void mostrarMensaje(String mensaje) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 25);
  display.println(mensaje);
  display.display();
  Serial.println(mensaje);
}

void guardarNombre(int id, String nombre) {
  nombre.trim();
  nombre.toCharArray(empleados.nombres[id - 1], LONGITUD_NOMBRE);
  empleadosStorage.write(empleados);
}

String obtenerNombre(int id) {
  return String(empleados.nombres[id - 1]);
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("Error OLED");
    while (true);
  }

  empleados = empleadosStorage.read();

  mostrarMensaje("Iniciando...");
  delay(2000);

  Serial1.begin(57600);
  finger.begin(57600);

  if (!finger.verifyPassword()) {
    mostrarMensaje("Error Huella");
    while (true);
  }

  conectarWiFi();
  timeClient.begin();
  server.begin();

  mostrarMensaje("Listo para huellas");
}

void loop() {
  timeClient.update();

  if (mostrarMensajeUnaVez) {
    mostrarMensaje("Pon tu dedo");
    mostrarMensajeUnaVez = false;
  }

  if (Serial.available()) {
    char opcion = Serial.read();
    if (opcion == '1') {
      registrarNuevaHuella();
      mostrarMensajeUnaVez = true;
    }
  }

  if (finger.getImage() == FINGERPRINT_OK) {
    if (finger.image2Tz() == FINGERPRINT_OK) {
      if (finger.fingerSearch() == FINGERPRINT_OK) {
        int id = finger.fingerID;
        String nombre = obtenerNombre(id);
        if (nombre.length() > 0) {
          ultimoNombre = nombre;
          ultimaHora = timeClient.getFormattedTime();
          mostrarMensaje("Bienvenido: " + nombre);
        } else {
          mostrarMensaje("Huella " + String(id) + " sin nombre");
        }
      } else {
        mostrarMensaje("Huella no registrada");
      }
    } else {
      mostrarMensaje("Error procesando");
    }
    delay(3000);
    mostrarMensajeUnaVez = true;
  }

  manejarClienteWeb();
  delay(100);
}

void conectarWiFi() {
  mostrarMensaje("Conectando WiFi...");
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  mostrarMensaje("WiFi Conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void manejarClienteWeb() {
  WiFiClient client = server.available();
  if (client) {
    String header = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        header += c;
        if (c == '\n') {
          client.println("HTTP/1.1 200 OK");
          client.println("Content-type:text/html");
          client.println();
          client.println("<html><head><meta charset='UTF-8'><title>Registro</title>");
          client.println("<style>");
          client.println("body { background-color: #0A192F; color: white; font-family: Arial, sans-serif; margin: 0; padding: 20px; }");
          client.println("h1 { color: #64FFDA; text-align: center; }");
          client.println("p { font-size: 18px; text-align: center; }");
          client.println(".container { max-width: 600px; margin: auto; padding: 20px; background-color: #112240; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.2); }");
          client.println("</style></head><body>");
          client.println("<div class='container'>");
          client.println("<h1>Registro de Huellas</h1>");
          client.println("<p><b>Último ingreso:</b></p>");
          client.println("<p>Nombre: " + ultimoNombre + "</p>");
          client.println("<p>Hora: " + ultimaHora + "</p>");
          client.println("</div>");
          client.println("</body></html>");

          break;
        }
      }
    }
    delay(1);
    client.stop();
  }
}

void registrarNuevaHuella() {
  mostrarMensaje("Ingresa ID (1-10)");

  while (Serial.available()) Serial.read();

  int id = 0;
  while (id < 1 || id > MAX_HUELLAS) {
    while (!Serial.available());
    id = Serial.parseInt();
    Serial.read();
    if (id < 1 || id > MAX_HUELLAS) {
      mostrarMensaje("ID invalido");
      Serial.println("ID invalido. Intenta de nuevo:");
    }
  }

  mostrarMensaje("Coloca dedo...");
  while (finger.getImage() != FINGERPRINT_OK);
  finger.image2Tz();
  mostrarMensaje("Retira dedo");
  delay(2000);
  mostrarMensaje("Coloca de nuevo");
  while (finger.getImage() != FINGERPRINT_OK);
  finger.image2Tz(2);

  if (finger.createModel() == FINGERPRINT_OK) {
    if (finger.storeModel(id) == FINGERPRINT_OK) {
      mostrarMensaje("Escribe nombre:");
      Serial.println("Escribe el nombre:");
      while (Serial.available()) Serial.read();
      while (!Serial.available());
      String nombre = Serial.readStringUntil('\n');
      guardarNombre(id, nombre);
      mostrarMensaje("Registrado: " + nombre);
    } else {
      mostrarMensaje("Error guardando huella");
    }
  } else {
    mostrarMensaje("Error creando modelo");
  }

  delay(3000);
}
