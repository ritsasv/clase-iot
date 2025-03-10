#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>
#include <FlashStorage.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C
#define PIN_SONIDO 4

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_Fingerprint finger(&Serial1);

const int MAX_HUELLAS = 10;
const int LONGITUD_NOMBRE = 20;

typedef struct {
  char nombres[MAX_HUELLAS][LONGITUD_NOMBRE];
} DatosEmpleados;

typedef struct {
  char hora[9];   // Formato HH:MM:SS
  char fecha[11]; // Formato DD/MM/YYYY
} DatosTiempo;

FlashStorage(empleadosStorage, DatosEmpleados);
FlashStorage(tiempoStorage, DatosTiempo);

DatosEmpleados empleados;
DatosTiempo tiempo;

char ssid[] = "Ritsa";
char pass[] = "12345678";

WiFiServer server(80);

String ultimoNombre = "Nadie";
String ultimaHora = "00:00:00";
String horaManual;
String fechaManual;
int huellaPendiente = 0;

void guardarHoraFecha(String hora, String fecha) {
  hora.toCharArray(tiempo.hora, 9);
  fecha.toCharArray(tiempo.fecha, 11);
  tiempoStorage.write(tiempo);
}

void mostrarMensaje(String mensaje) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 25);
  display.println(mensaje);
  display.display();
}

void mostrarDescanso() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 5);
  display.println("Esperando registrar");
  display.setCursor(0, 15);
  display.println("o identificar huella");
  display.setCursor(0, 35);
  display.print("Hora: ");
  display.println(horaManual);
  display.setCursor(0, 45);
  display.print("Fecha: ");
  display.println(fechaManual);
  display.display();
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
  delay(1000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 25);
  display.println("Encendiendo...");
  display.display();

  pinMode(PIN_SONIDO, OUTPUT);
  digitalWrite(PIN_SONIDO, HIGH);

  empleados = empleadosStorage.read();
  tiempo = tiempoStorage.read();
  horaManual = String(tiempo.hora);
  fechaManual = String(tiempo.fecha);

  delay(2000);

  Serial1.begin(57600);
  finger.begin(57600);

  if (!finger.verifyPassword()) {
    mostrarMensaje("Error Huella");
    while (true);
  }

  mostrarMensaje("Conectando WiFi...");
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    delay(1000);
  }
  mostrarMensaje("WiFi Conectado");

  server.begin();

  mostrarDescanso();
}

void loop() {
  if (huellaPendiente > 0) {
    registrarHuellaDesdeWeb(huellaPendiente);
    huellaPendiente = 0;
    mostrarDescanso();
  }

  if (finger.getImage() == FINGERPRINT_OK) {
    if (finger.image2Tz() == FINGERPRINT_OK) {
      if (finger.fingerSearch() == FINGERPRINT_OK) {
        int id = finger.fingerID;
        String nombre = obtenerNombre(id);
        if (nombre.length() > 0) {
          ultimoNombre = nombre;
          ultimaHora = horaManual;
          mostrarMensaje("Bienvenido: " + nombre);
        } else {
          mostrarMensaje("Huella " + String(id) + " sin nombre");
        }
      } else {
        mostrarMensaje("Huella no registrada");
      }
    }
    delay(3000);
    mostrarDescanso();
  }

  manejarClienteWeb();
  delay(100);
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
          if (header.indexOf("GET /registrar?id=") >= 0) {
            int idIndex = header.indexOf("id=") + 3;
            huellaPendiente = header.substring(idIndex, header.indexOf(' ', idIndex)).toInt();
          }

          if (header.indexOf("GET /nombre?id=") >= 0) {
            int idIndex = header.indexOf("id=") + 3;
            int nombreIndex = header.indexOf("nombre=") + 7;
            int id = header.substring(idIndex, header.indexOf('&')).toInt();
            String nombre = header.substring(nombreIndex, header.indexOf(' ', nombreIndex));
            guardarNombre(id, nombre);
            mostrarMensaje("Nombre guardado: " + nombre);
            delay(3000);
            mostrarDescanso();
          }

          if (header.indexOf("GET /settime?hora=") >= 0) {
            int horaIndex = header.indexOf("hora=") + 5;
            int fechaIndex = header.indexOf("fecha=") + 6;
            horaManual = header.substring(horaIndex, header.indexOf('&', horaIndex));
            fechaManual = header.substring(fechaIndex, header.indexOf(' ', fechaIndex));
            guardarHoraFecha(horaManual, fechaManual);
            mostrarMensaje("Hora y fecha actualizadas");
            delay(3000);
            mostrarDescanso();
          }

          client.println("HTTP/1.1 200 OK");
          client.println("Content-type:text/html");
          client.println();
          client.println("<html><head><meta charset='UTF-8'><title>Registro</title></head><body>");
          client.println("<h1>Control de Huellas</h1>");
          client.println("<p><b>Último ingreso:</b></p>");
          client.println("<p>Nombre: " + ultimoNombre + "</p>");
          client.println("<p>Hora: " + String(ultimaHora).replace('%3A','') + "</p>");
          client.println("<h2>Registrar nueva huella</h2>");
          client.println("<form action='/registrar' method='GET'>ID (1-10): <input type='number' name='id'><input type='submit' value='Registrar'></form>");
          client.println("<h2>Asignar nombre</h2>");
          client.println("<form action='/nombre' method='GET'>ID: <input type='number' name='id'> Nombre: <input type='text' name='nombre'><input type='submit' value='Guardar'></form>");
          client.println("<h2>Configurar Hora y Fecha</h2>");
          client.println("<form action='/settime' method='GET'>Hora (HH:MM:SS): <input type='text' name='hora'> Fecha (DD/MM/YYYY): <input type='text' name='fecha'><input type='submit' value='Actualizar'></form>");
          client.println("</body></html>");
          break;
        }
      }
    }
    delay(1);
    client.stop();
  }
}

void registrarHuellaDesdeWeb(int id) {
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
      mostrarMensaje("Huella guardada");

      Serial1.end();
      delay(100);

      digitalWrite(PIN_SONIDO, LOW);
      delay(100);
      digitalWrite(PIN_SONIDO, HIGH);

      delay(4000);

      Serial1.begin(57600);
      delay(2000);

    } else {
      mostrarMensaje("Error guardando");
    }
  } else {
    mostrarMensaje("Error creando modelo");
  }
  delay(3000);
  mostrarDescanso();
}
