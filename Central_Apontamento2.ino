// MEDE TEMPERATURA INTERNA DO ESP32
extern "C"
{
  uint8_t temprature_sens_read();
}
#include <ETH.h>
#include "time.h"
#include "string.h"
#include "WiFi.h" PROGMEM
#include <ESP32WebServer.h> PROGMEM
#include "SSD1306.h"
#include <Update.h>
#include "esp_wps.h"
#include <Wire.h>

#define SDA 13
#define SCL 16
#define rst_s0 17
#define rst_s1 15
#define ent_sensor 39
#define sd_sensor 5

/*Variaveis Apontamento*/
int slave[2] = {0x32, 0x42};
bool estado[2] = {false, false};
int pin_reset[2] = {rst_s0, rst_s1};
int log_reset[2] = {0, 0};

char dado[50];
String apontamentos;
int conta = 0, ativo;

int tam_slave = sizeof(slave) / sizeof(int);
int tam_pin = sizeof(pin_reset) / sizeof(int);
int tam_log = sizeof(log_reset) / sizeof(int);

//*Variaveis*/
static bool eth_connected = false;
int ip[] = {0, 0, 0, 0};
int gateway[] = {0, 0, 0, 0};
int subnet[] = {0, 0, 0, 0};
// TQL - tempo que define a quebra de lotes
int id_prxlote = 0, priFila = 0, TQL = 5000, limiteVetor = 500;
long int loops = 0;
String StatusWifi, s_aux = "", sinalWifi;
String lotes[500] = {"", "", ""};
bool dLote = true; // controla a quebra do lote para que data ini e data fim estejam no mesmo dia

const char *ssid = "WIFICABONNET";
const char *password = "forcaf123";
const char *c_aux = "";

ESP32WebServer server(80);
clock_t tConfirmLote, tLoop, ttimeUltLote = 0;
time_t timeServerAtu, timeServerAtuRn, timeNow, timeServerDif, timeServer, timeServerResetNullptr, timeFimLote, timeIniLote = 0;
struct tm *dataNow;
SemaphoreHandle_t httpMutex = xSemaphoreCreateMutex();
hw_timer_t *timer = NULL;

/*Apontamento*/
void inicia_PinMode()
{
  for (int i = 0; i < tam_pin; i++)
  {
    pinMode(pin_reset[i], OUTPUT);
    digitalWrite(pin_reset[i], HIGH);
  }
}

int procura_PosSlave(int quem)
{
  int i = 0;
  while (i < tam_slave && slave[i] != quem)
    i++;
  return i;
}

void exibe_logReset()
{
  Serial.print("[ ");
  for (int i = 0; i < tam_log; i++)
  {
    Serial.print(log_reset[i]);
    Serial.print(" |");
  }
  Serial.println("]");
}

void reset(int qual)
{
  int pos = procura_PosSlave(qual);
  if (slave[pos] == qual && estado[pos] == false)
  {
    estado[pos] = true;
    digitalWrite(pin_reset[pos], LOW);
    delay(1000);
    digitalWrite(pin_reset[pos], HIGH);
    log_reset[pos] += 1;
    exibe_logReset();
  }
}

void normaliza(int qual)
{
  int pos = procura_PosSlave(qual);
  if (slave[pos] == qual && estado[pos] == true)
    estado[pos] = false;
}

//FUNCAO PARA LER E ENVIAR DADOS AO(S) ESCRAVO(S)
void escravo(int slave)
{
  int tam_resp = 0; //pega o tamanho da resposta vindo do maix bit
  int leitura, i;
  char letra;
  char check[10], informacao[50];
  int cont_info = 0, val_check, check_info = 0;

  //VAI PERGUNTAR SE TEM DADO
  Wire.beginTransmission(slave);   //abre a transmissao
  Wire.write(0);                   // '0' pergunta se tem dado
  if (Wire.endTransmission() == 0) //fecha a transmissao e confirma que o maix bit esta vivo
  {
    normaliza(slave);
    Serial.print(conta++);
    Serial.print(" 0x");
    Serial.print(slave, HEX);
    Serial.println(": Pergunta recebida!");

    Wire.requestFrom(slave, 1); //PEGANDO RESPOSTA
    if (Wire.available())
    {
      tam_resp = Wire.read(); //"Tamanho da resposta"
    }

    if (tam_resp > 0) //Maix Bit tem Dados
    {
      //Requisitando dados
      Wire.beginTransmission(slave);   //abre a transmissao
      Wire.write(1);                   // '1' Solicita a informacao
      if (Wire.endTransmission() == 0) //fecha a transmissao e confirma que o maix bit esta vivo
      {
        Serial.print("0x");
        Serial.print(slave, HEX);
        Serial.println(": Enviando dados!");

        Wire.requestFrom(slave, tam_resp); //PEGANDO RESPOSTA
        while (Wire.available() && cont_info < tam_resp)
        {
          leitura = Wire.read();           //pega inteiro por inteiro do canal I2C
          letra = (char)leitura;           //converte o inteiro para char
          informacao[cont_info++] = letra; //guarda na informacao
        }
        if (cont_info == tam_resp) //chegou todas as informações
        {
          informacao[cont_info] = '\0'; //agora vira uma string que pode ser lida

          String split = strtok(informacao, ","); //primeiro split, para pegar a informacao
          String check_sum = strtok(NULL, ",");   //segundo split, para pegar o check sum da mensagem original

          //converte o split para o dado
          for (i = 0; i < split.length(); i++)
          {
            dado[i] = informacao[i];          // aqui o dado é global esse sera enviado a requisicao WebAPI
            check_info += (int)informacao[i]; //aqui faz o check sum da mensagem recebida, ou seja, soma os inteiro da mensagem que chegou
          }
          dado[i] = '\0'; //Aqui esta a informação pode ser lida

          //pega o check sum da mensagem original, ou seja, do segundo split e convert para uma string char para pode usar o atoi
          for (i = 0; i < check_sum.length(); i++)
            check[i] = check_sum[i];
          check[i] = '\0';
          val_check = atoi(check); //Aqui pega o check original que vem da mensagem, converte para inteiro

          if (val_check == check_info) //se o check da mensagem origial for igual ao check que fez decodificando a mensagem entao exibe, ou seja, o dado é confiavel
          {
            Serial.print("0x");
            Serial.print(slave, HEX);
            Serial.print(" - Dado: ");
            Serial.println(dado);
            if (apontamentos.length() <= 0)
              apontamentos = dado;
            else
            {
              if (sizeof(dado) > 0 && apontamentos != dado)
                apontamentos = dado;
            }
            dado[0] = '\0';
          }
        }
        else //nao chegou toda as informações
        {
          Serial.print("0x");
          Serial.print(slave, HEX);
          Serial.println(": Nao chegou todas as informações");
          reset(slave);
        }
      }
      else //Nao esta vivo
      {
        Serial.print("0x");
        Serial.print(slave, HEX);
        Serial.println(": Morreu na pergunta 1");
        reset(slave);
      }
    }
    else
    {
      Serial.print("0x");
      Serial.print(slave, HEX);
      Serial.println(": Sem dados");
    }
  }
  else //Nao esta vivo
  {
    Serial.print("0x");
    Serial.print(slave, HEX);
    Serial.println(": Morreu na pergunta 0");
    reset(slave);
  }
}

/*WatchDog*/
void IRAM_ATTR resetModule() //funcao que o temporizador ira chamar, para reiniciar o ESP32
{
  ESP.restart();
}

char *itoa(int value, char *str)
{
  try
  {
    char temp;
    memset(&str, 0, sizeof(str));
    int i = 0;
    while (value > 0)
    {
      int digito = value % 10;
      str[i] = digito + '0';
      value /= 10;
      i++;
    }
    i = 0;
    int j = strlen(str) - 1;
    while (i < j)
    {
      temp = str[i];
      str[i] = str[j];
      str[j] = temp;
      i++;
      j--;
    }
    return str;
  }
  catch (...)
  {
    resetModule();
  }
}

String CtoS(const char *c)
{
  try
  {
    String s = "";
    int i;
    for (i = 0; i < 6; i++)
    {
      s += c[i];
    }
    return s;
  }
  catch (...)
  {
    resetModule();
  }
}

void setupWatchDog()
{
  try
  {
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &resetModule, true);
    //timer,tempo(us),repeticao
    timerAlarmWrite(timer, 5000000, true);
    timerAlarmEnable(timer); //habilita a interrupcao
  }
  catch (...)
  {
    resetModule();
  }
}

void loopWatchDog()
{
  try
  {
    timerWrite(timer, 0); //reseta o temporizador (alimenta o watchdog)
  }
  catch (...)
  {
    resetModule();
  }
}

/*Conexao*/

time_t getTime_t()
{
    return timeServer + time(nullptr) - timeServerResetNullptr;
}

void loopWifiServer()
{
    try
    {
        server.handleClient(); //Resposta do servidor
    }
    catch (...)
    {
        resetModule();
    }
}

void WiFiEvent(WiFiEvent_t event)
{
    switch (event)
    {
    case SYSTEM_EVENT_ETH_START:
        Serial.println("ETH Started");
        //set eth hostname here
        ETH.setHostname("esp32-ethernet");
        break;
    case SYSTEM_EVENT_ETH_CONNECTED:
        Serial.println("ETH Connected");
        break;
    case SYSTEM_EVENT_ETH_GOT_IP:
        Serial.print("ETH MAC: ");
        Serial.print(ETH.macAddress());
        Serial.print(", IPv4: ");
        Serial.print(ETH.localIP());
        if (ETH.fullDuplex())
        {
            Serial.print(", FULL_DUPLEX");
        }
        Serial.print(", ");
        Serial.print(ETH.linkSpeed());
        Serial.println("Mbps");
        eth_connected = true;
        break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
        Serial.println("ETH Disconnected");
        eth_connected = false;
        break;
    case SYSTEM_EVENT_ETH_STOP:
        Serial.println("ETH Stopped");
        eth_connected = false;
        break;
    default:
        break;
    }
}

//inicializa os lotes com '.'
void setupParametros()
{
    try
    {
        int i = 0;
        for (i = 0; i < limiteVetor; i++)
        {
            lotes[i] = ".";
        }
        id_prxlote = 0;
        priFila = 0;
    }
    catch (...)
    {
        resetModule();
    }
}

void handleRoot()
{
    try
    {
        xSemaphoreTake(httpMutex, portMAX_DELAY);
        String Cmd = "";
        if (server.arg("cmd") == "reset")
        {
            server.sendHeader("Connection", "close");
            server.send(200, "text/html", "OK");
            delay(2000);
            resetModule();
        }
        String serverIndex =
            "<br> Milisegundos:  " + String(millis()) + " "
                                                        "<br> Clock:  " +
            String(clock()) + " "
                              "<br> Dado: " +
            String(apontamentos) + " "

                                   "<br><script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
                                   "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
                                   "<input type='file' name='update'>"
                                   "<input type='submit' value='Update'>"
                                   "</form>"
                                   "<div id='prg'>progress: 0%</div>"
                                   "<script>"
                                   "$('form').submit(function(e){"
                                   "e.preventDefault();"
                                   "var form = $('#upload_form')[0];"
                                   "var data = new FormData(form);"
                                   " $.ajax({"
                                   "url: '/update',"
                                   "type: 'POST',"
                                   "data: data,"
                                   "contentType: false,"
                                   "processData:false,"
                                   "xhr: function() {"
                                   "var xhr = new window.XMLHttpRequest();"
                                   "xhr.upload.addEventListener('progress', function(evt) {"
                                   "if (evt.lengthComputable) {"
                                   "var per = evt.loaded / evt.total;"
                                   "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
                                   "}"
                                   "}, false);"
                                   "return xhr;"
                                   "},"
                                   "success:function(d, s) {"
                                   "console.log('success!')"
                                   "},"
                                   "error: function (a, b, c) {"
                                   "}"
                                   "});"
                                   "});"
                                   "</script>";

        xSemaphoreGive(httpMutex);
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", serverIndex);
    }
    catch (...)
    {
        resetModule();
    }
}

//pagina que retorna a lista de lotes pendentes de envio
void handlegetLotes()
{
    try
    {
        String html = "", s_aux1 = "";
        //const char* c_aux;
        int i = 0;
        timeServerAtu = server.arg("d").toInt();
        timeServerAtuRn = time(nullptr);
        timeServerDif = getTime_t() - timeServerAtu;

        if (server.arg("tql") != "")
        {
            TQL = server.arg("tql").toInt() * 1000;
        }

        if (timeServer == 0)
        {
            timeServer = server.arg("d").toInt();
            timeServerResetNullptr = time(nullptr);
            s_aux = "|Data Atualizada:" + String(timeServer);
            c_aux = s_aux.c_str();
        }
        else
        {
            if (server.arg("at").toInt() == 1)
            {
                timeServer = server.arg("d").toInt();
                timeServerResetNullptr = time(nullptr);
            }
        }
        // codigo do lote
        xSemaphoreTake(httpMutex, portMAX_DELAY);
        for (i = 0; i < limiteVetor; i++)
        {
            if (lotes[i] != ".")
            {
                html += lotes[i];
            }
        }
        xSemaphoreGive(httpMutex);
        if (html == "") //nao existe lotes e o confirma lotes sera acionado  do contrario o confirma lotes sao acionados quando existe uma delecao de registros assim aumentando a seguranca do metodo contingencia
        {
            tConfirmLote = clock(); // esta variavel indica se o sistema esta sem comunicacao e serve para entrar em modo critico de operacao e criacao de lotes
        }
        // envia logs no fim da cominucacao
        html += "logs#" + String(((temprature_sens_read() - 32) / 1.8)) + "#" + String(hallRead()) + "#" + sinalWifi;
        //hallRead - MEDE A INTERFERENCIA ELETRO MAGNETICA
        server.setContentLength(html.length());
        server.send(200, "text/html", html);
    }
    catch (...)
    {
        resetModule();
    }
}

void handleconfirmLotes()
{
    try
    {
        String s_aux = server.arg("l");
        if (s_aux != "")
        {
            String s_aux1 = "";
            int l = s_aux.length();
            int x = 0;
            for (x = 0; x < l; x++)
            {
                if (s_aux[x] == '.')
                {
                    if (s_aux1 != "")
                    {
                        // apaga arquivo
                        lotes[s_aux1.toInt()] = ".";
                        xSemaphoreTake(httpMutex, portMAX_DELAY);
                        priFila = s_aux1.toInt() + 1;
                        xSemaphoreGive(httpMutex);
                        s_aux1 = "";
                    }
                }
                else
                {
                    s_aux1 += s_aux[x];
                }
            }
        }
        else
        {
            s_aux = "Zero lotes enviados";
        }
        tConfirmLote = clock();
        server.setContentLength(s_aux.length());
        server.send(200, "text/html", s_aux);
    }
    catch (...)
    {
        resetModule();
    }
}

void gravaLote()
{
    struct tm *dattime;
    try
    {
        //if (timeServerDif > 0 && CD1 <= 0 ) { // a data do  ESP e maior entao devemos controlar a geracao dos lotes ate a data sincronizar
        if (timeServerDif > 0)
        { // a data do  ESP e maior entao devemos controlar a geracao dos lotes ate a data sincronizar
            if (timeServerDif > (TQL / 1000))
                timeServer = timeServer - (TQL / 1000);
            else
                timeServer = timeServer - timeServerDif;
            //return false;
        }

        if (timeServer != 0)
        {                         // grava lote pois esta com data atualizada
            if (timeFimLote == 0) // siginifica que a data atualizou a variavel timeServer pore ainda nao atualizou a variavel timeFimLote   ai vou calcular as datas para nao mandar 1970 porem todo o tempo que o esp ficar desligado nao tera atualizacao
                timeIniLote = timeServer;
            else
                timeIniLote = timeFimLote;

            //      if (timeServerDif< 0 && CD1 <= 0 ) { // a data do schedule esta maior entao e sinal que a data do esp e menor e o ultimo lote tem data menor podendo ser atualizado
            if (timeServerDif < 0)
            { // a data do schedule esta maior entao e sinal que a data do esp e menor e o ultimo lote tem data menor podendo ser atualizado
                timeServerResetNullptr = timeServerAtuRn;
                timeServer = timeServerAtu;
                timeServerDif = 0;
            }

            timeFimLote = getTime_t();
            dattime = localtime(&timeIniLote);

            // limite do vetor cheio, porem primeiro vetor liberado, aponta novamente para o primeiro vetor
            if (id_prxlote == limiteVetor && lotes[0] == ".")
            {
                id_prxlote = 0;
            }

            String s_aux1 = String(id_prxlote) + "#" + String(dattime->tm_year + 1900) + "-" + String(dattime->tm_mon + 1) + "-" + String(dattime->tm_mday) + " " + String(dattime->tm_hour) + ":" + String(dattime->tm_min) + ":" + String(dattime->tm_sec) + "#";
            dattime = localtime(&timeFimLote);
            s_aux1 += String(dattime->tm_year + 1900) + "-" + String(dattime->tm_mon + 1) + "-" + String(dattime->tm_mday) + " " + String(dattime->tm_hour) + ":" + String(dattime->tm_min) + ":" + String(dattime->tm_sec) + "#";
            s_aux1 += apontamentos + "|";
            if (id_prxlote == limiteVetor && lotes[0] != ".") // estado critico filas cheias
            {
                timeFimLote = timeIniLote;
            }

            if (lotes[id_prxlote] != ".")
            {
                timeFimLote = timeIniLote;
            }
            lotes[id_prxlote] = s_aux1;
            id_prxlote = id_prxlote + 1;
        }
    }
    catch (...)
    {
        resetModule();
    }
}

void handlelog()
{
    try
    {
        xSemaphoreTake(httpMutex, portMAX_DELAY);
        String html = "";
        int i = 0;
        html += "<h1><BR>VARIAVEIS... </h1>";
        /*########################################### VARIAVEIS DE CONFIGURACAO ################################################*/
        html += "  millis()             =" + String(millis()) + "<br>";
        //html += "  ipMaquina            =" + String(ETH.localIP()) + "<br>";
        html += "  TQL                  =" + String(TQL) + "<br>";
        html += "  c_aux                =" + String(c_aux) + "<br>";
        html += "  s_aux                =" + String(s_aux) + "<br>";
        html += "  id_prxlote           =" + String(id_prxlote) + "<br>";
        html += " priFila               =" + String(priFila) + "<br>";
        html += " loops                 =" + String(loops) + "<br>";
        html += " tConfirmLote          =" + String(tConfirmLote) + "<br>";
        html += " ttimeUltLote          =" + String(ttimeUltLote) + "<br>";
        html += " timeServer            =" + String(timeServer) + "<br>";
        html += " timeNow               =" + String(timeNow) + "<br>";
        html += " timeFimLote           =" + String(timeFimLote) + "<br>";
        html += " timeIniLote           =" + String(timeIniLote) + "<br>";
        //html += " StatusWifi            =" + String(StatusWifi) + "<br>";
        //html += " sinalWifi            =" + String(sinalWifi) + "<br>";
        //VETORES
        for (i = 0; i < limiteVetor; i++)
        {
            if (lotes[i] != ".")
            {
                html += "lote[" + String(i) + "]    = " + lotes[i] + "<br>";
            }
        }

        server.setContentLength(html.length());
        server.send(20000, "text/html", html);

        xSemaphoreGive(httpMutex);
    }
    catch (...)
    {
        resetModule();
    }
}

void handleResetSlave()
{
    try
    {
        xSemaphoreTake(httpMutex, portMAX_DELAY);
        String Cmd = "";
        if (server.arg("cmd") == "reset")
        {
            server.sendHeader("Connection", "close");
            server.send(200, "text/html", "OK");
            delay(2000);
            resetModule();
        }

        String serverIndex =
            "<br> Quantidade de resete slave 0: " + String(log_reset[0]) + " "
                                                                           "<br> Quantidade de resete slave 1: " +
            String(log_reset[1]) + " ";

        xSemaphoreGive(httpMutex);
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", serverIndex);
    }
    catch (...)
    {
        resetModule();
    }
}

void setupWifiServer()
{
    try
    {
        WiFi.onEvent(WiFiEvent);
        ETH.begin();
        // define IP fixo
        if (subnet[0] > 0)
        {
            IPAddress _ip(ip[0], ip[1], ip[2], ip[3]);
            IPAddress _gateway(gateway[0], gateway[1], gateway[2], gateway[3]);
            IPAddress _subnet(subnet[0], subnet[1], subnet[2], subnet[3]);
            ETH.config(_ip, _gateway, _subnet);
        }

        //time_t timeout = millis() + 10000;
        server.on("/", handleRoot);
        server.on("/getLotes", handlegetLotes);
        server.on("/confirmLotes", handleconfirmLotes);
        server.on("/log", handlelog);
        server.on("/reset", handleResetSlave);
        /*handling uploading firmware file */
        server.on(
            "/update", HTTP_POST, []() {
      Serial.printf(" timerDetachInterrupt ");
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      esp_wifi_wps_disable(); ESP.restart(); }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update: %s\n", upload.filename.c_str());
        timerAlarmDisable(timer);

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
          Update.printError(Serial);
        }
      }
      else if (upload.status == UPLOAD_FILE_WRITE) {
        /* flashing firmware to ESP*/
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      }
      else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        }
        else {
          Update.printError(Serial);
        }
      } });

        // start the server
        server.begin();
    }
    catch (...)
    {
        resetModule();
    }
}

/*Central*/

void setupfileSistem()
{
  try
  {
    setupParametros();
    setupWifiServer();
  }
  catch (...)
  {
    resetModule();
  }
}

void setupcoreZero(void *pvParameters)
{
  try
  {
    String msg = "";
    StatusWifi = "Conecting....";
    setupfileSistem();
    for (;;)
    {
      if (ssid != "")
      {
        if (WiFi.status() != WL_CONNECTED && eth_connected == false)
        {
          StatusWifi = "Conecting....";
          setupWifiServer(); // conecta
          delay(1000);
          Serial.println(WiFi.localIP());
          Serial.println(WiFi.macAddress());
          Serial.println("Try conect");
        }
        else
        {
          loopWifiServer();
        }
      }
      delay(1);
    }
    vTaskDelete(NULL);
  }
  catch (...)
  {
    resetModule();
  }
}

void setup()
{
  try
  {
    inicia_PinMode();
    pinMode(ent_sensor, INPUT);
    pinMode(sd_sensor, OUTPUT);

    Serial.begin(115200);
    Wire.begin(SDA, SCL);
    xTaskCreatePinnedToCore(setupcoreZero, "setupcoreZero", 8192, NULL, 0, NULL, 0);
    // configura WatchDog
    setupWatchDog();
  }
  catch (...)
  {
    resetModule();
  }
}

void loop()
{
  try
  {
    tLoop = millis();
    loopWatchDog();

    if (loops == 0)
    {
      loops = millis();
    }

    timeNow = getTime_t();
    dataNow = localtime(&timeNow);

    for (int i = 0; i < tam_slave; i++)
    {
      ativo = digitalRead(ent_sensor);
      digitalWrite(sd_sensor, ativo);
      escravo(slave[i]);
      delay(200);
    }

    //TRATAMENTO PARA QUEBRAR LOTE QUANDO O ESP ESTA EM CONTINGENCIA
    if ((clock() - tConfirmLote < TQL) || sizeof(lotes[id_prxlote]) > 255)
    {
      gravaLote();
    }
    delay(2);
    tLoop = millis() - tLoop;
  }
  catch (...)
  {
    resetModule();
  }
}
