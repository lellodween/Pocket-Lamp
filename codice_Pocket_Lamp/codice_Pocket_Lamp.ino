#include <WiFi.h>   //https://www.arduino.cc/en/Reference/WiFi
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

#include <WebServer.h>    // WebServer locale. crea una pagina di configurazione della rete -> https://github.com/zhouhan0126/WebServer-esp32
#include <DNSServer.h>    //  DNS Server locale. mi serve per reindirizzare le richieste al WebServer locale -> https://github.com/zhouhan0126/DNSServer---esp32
#include <WiFiManager.h>  //  WiFiManager ->  https://github.com/zhouhan0126/WIFIMANAGER-ESP32

/********            BLYNK            **********/

//#define BLYNK_PRINT Serial // commentandlo disabilito il print sulla seriale di blynk

// RICORDA se lampada mittente ha VP_RICEZIONE = V5 e VP_invio = V6
// la lampada ricevente deve avere VP_RICEZIONE = V6 e VP_invio = V5
#define VP_RICEZIONE V5 // indica la porta virtuale di recezione notifica
#define VP_INVIO V6     // indica la porta virtuale d'invio notifica

// RICORDA se lampada mittente ha auth[] = "token1" e char esp32_slave[] = "token2"
// la lampada ricevente deve avere auth[] = "token2" e char esp32_slave[] = "token1"
 char auth[] = "AAAA";         //token Blynk propria lampada ( lampada mittente )
 char esp32_slave[] = "BBBB";  //token Blynk della lampada su cui avverrà la notifica ( lampada ricevente )


/********            WIFI MANAGER            **********/
WiFiManager wifiManager;

char ssid[] ="nome_access_point";  // nome ssid wifi lampada


/********            PWM SETTING            **********/

const int LED_PIN = 23;  // numero pin LED -> GPIO23

// impostazione PWM per del LED
const int FREQ = 5000;    // setto la frequenza 5000Hz
const int LED_CHANNEL = 0; // canale sul quale verrà generato il segnale che pilota il led 
const int RESOLUTION = 8; // imposto la risoluzione ovvero 255


/********            PULSANTE            **********/

const int PULSANTE = 5; // numero pin PULSANTE (GPIO5)
int stato_pulsante = 0; // memorizzo lo stato attuale del pulsante 1 se premuto altrimenti 0

// variabili per debounce
unsigned long tempo_passato = 0;    // tempo trascorso da quando si preme il pulsante
const unsigned long DEBOUNCE_DELAY = 200; // tempo minimo per evitare errorei di debounce

// variabili calcolo tempo pressione pulsante
int start_pressione = 0;  // memorizzo lo stato passato del pulsante 1 se premuto altrimenti 0
unsigned long tempo_pressione_momentaneo = 0; // indica da quanto tempo sto tenendo premuto il pulsante

const unsigned long TEMPO_INVIO_NOTIFICA = 2000; // tempo pressione pulsante di attesa prima di invio notifica alla lampada del ricevente
const unsigned long TEMPO_NEW_WIFI = 30000; // tempo pressione pulsante di attesa prima di reset wifi 

/********            dichiarazione funzioni            **********/

int anti_debounce();
int misuro_pressione();
void pulsazione_led();

void invia_notifica();
void spengo_notifica();

void impostawifi();
void blink_led_riconnetti();
void riconnetti();


////////////////////////////////////////////////////////////

void setup(){
  Serial.begin(115200); //imposto baud rate
  
  ///////////////////////////////// pulsante ///////////////
  pinMode(PULSANTE,INPUT_PULLDOWN); //imposto il pin del pulsante in ingresso e setto la resistenza pulldown interna
  
  //////////////////////////// LED PWM /////////////////
  // setto le caratteristiche del segnale e le assegno al canale 0
  ledcSetup(LED_CHANNEL, FREQ, RESOLUTION);
  
  // collego il canale 0 (su cui viene generato il segnale) alla gpio 23 (nella quale è collegato il LED)
  ledcAttachPin(LED_PIN, LED_CHANNEL);
  
  //////////////////////////// CONNESSIONE WIFI /////////////////  
  
  ledcWrite(LED_CHANNEL, 255); // led di controllo per il blynk di fine setup
                                          
  Blynk.config(auth); // abilito la connessione con il server di blynk 
  
  //Blynk.begin(auth, "SSID", "password"); // questo comando mi permette di collegarmi subito alla rete wifi senza effettuare
                                           // i passaggi di configurazione del wifi tramite webpage
                                           // se usi questo metodo commenta "Blynk.config(auth);" e scommenta Blynk.begin(auth, "SSID", "password");
                                           // inserendo le credenziali giuste compila e carica tutto nell'esp
    
  delay(500);
  ledcWrite(LED_CHANNEL, 0);     
  
}


///////////////////////////////////////////// FUNZIONI ////////////////////////////////


/**
 * @descrizione  Questa funzione svolge un controllo di debounce sul pulsante
 * @parametro  //
 * @return 1 ( se pulsante premuto correttamente) , 0 ( bounce )
 */
int anti_debounce(){
  
  if (digitalRead(PULSANTE)){
    if ( ( millis() - tempo_passato ) > DEBOUNCE_DELAY ){
      tempo_passato = millis();
      return 1;
    }
  }
  return 0;
}

/**
 * @descrizione  calcola il tempo di pressione del pulsante 
 * @parametro  //
 * @return     >0 quando il pulsante viene rilasciato e indica il tempo di pressione       
 *              0 se il pulsante viene premuto per un tempo < a TEMPO_INVIO_NOTIFICA o TEMPO_NEW_WIFI o se pulsante non viene rilasciato
 *             -1 se il pulsante viene premuto per un tempo >= TEMPO_INVIO_NOTIFICA ma < TEMPO_NEW_WIFI
 *             -2 se il pulsante viene premuto per un tempo >= TEMPO_NEW_WIFI
 */
int misuro_pressione(){
  
  if ((start_pressione == 0) && (digitalRead(PULSANTE)) == 1) {
    start_pressione = 1;
    tempo_pressione_momentaneo = millis();

  }
  
  if ((start_pressione == 1) && ((digitalRead(PULSANTE)) == 0)) {
    start_pressione = 0;
        
    return(millis() - tempo_pressione_momentaneo);
    
  }
  // se il pusante resta premuto per un tempo superiore a TEMPO_NEW_WIFI chiamo la funzione per l'avvio dela webpage di modifica rete 
  if((millis() - tempo_pressione_momentaneo) >= TEMPO_NEW_WIFI){
    //impostawifi();
    return -2;
    
  }
  
  else if ((millis() - tempo_pressione_momentaneo) >= TEMPO_INVIO_NOTIFICA){  
    return -1;
  }
    
  return 0;
}

/**
 * @descrizione  svolge una serie di cicli pwm ( spento --> acceso  ) x cicli_pulsazione
 * @parametro  //
 * @return     //
 */
void pulsazione_led(){
  
  int ciclo_pulsazione = 0;   //contatore numero ciclo 
  int cicli_pulsazione = 2;   // numero cicli max
  int speed_pulsazuine = 20;  // velocità di esecuzione 
  int val_pwm;
  do{
    for( val_pwm = 0; val_pwm <= 255; val_pwm++){   // incremetno pwm 
      // changing the LED brightness with PWM
      ledcWrite(LED_CHANNEL, val_pwm);
      delay(speed_pulsazuine);
      
    }

    // decrease the LED brightness
    for( val_pwm = 255; val_pwm >= 0; val_pwm--){   // decremento pwm
      // changing the LED brightness with PWM
      ledcWrite(LED_CHANNEL, val_pwm);   
      delay(speed_pulsazuine);
      
    }
   ciclo_pulsazione++;
   }while(ciclo_pulsazione < cicli_pulsazione);
}

////////////////////////////////// FUNIZONI BLYNK ///////////////////////////////////////////

// imposto la porta virtuale di bridge tra la lampada e il server di blynk
WidgetBridge bridge1(V1);

// imposto il token di autorizzazione della lampada ricevente necessario per controllare tale lampada   
BLYNK_CONNECTED() {
  bridge1.setAuthToken(esp32_slave); 
}

/**
 * @descrizione ricevo notifica dalla lampada mittente, eseguo una pulsazione del led e lo mantengo acceso
 *              se non ricevo notifica lascio il led spento 
 *              
 * @parametro   VP_RICEZIONE corrisponde al pin virtuale   
 * @return     //
 */
BLYNK_WRITE(VP_RICEZIONE) {
  int pinData = param.asInt();

  // la digitalRead(PULSANTE)) == 0 evita l'errore in cui l'utente sta inviando la notifica e contemporaneamente riceve una notifica
  if((digitalRead(PULSANTE)) == 0)
  {
    if ( pinData == HIGH ) { 
      pulsazione_led();
      ledcWrite(LED_CHANNEL, 255); 
    }
    else{
      ledcWrite(LED_CHANNEL, 0);
    }
  }
 
}

//////////////////////////////////////////// invio notifica //////////


/**
 * @descrizione  invia una notifica alla lampada ricevente solo se il pulsante viene premuto per un tempo >= a TEMPO_INVIO_NOTIFICA
 *               dopo di che effettuo una pulsazione del led 
 * @parametro  //
 * @return     //
 */
void invia_notifica(){
  
  if(misuro_pressione() == -1 )
  {
    ledcWrite(LED_CHANNEL, 255);
    
    do{
      Blynk.run();  // serve per mantenere il sincronismo con blynk
    }while( misuro_pressione() <= 0); // mantengo un ciclo infinito fino a che non si rilascia il pulsante 
    
     // **************************** INVIO NOTIFICA *************************************
   
    bridge1.virtualWrite(VP_INVIO, 1); // invia notifica a lampada ricevente
    pulsazione_led();
  }
         
}

//////////////////////////////////////////// spengo notifica ricevuta //////////
/**
 * @descrizione  spengo il led di ricevuta notifica
 *               se il pulsante non viene rilasciato resto all'interno di un ciclo infinito.
 * @parametro  //
 * @return     //
 */
void spengo_notifica(){
    ledcWrite(LED_CHANNEL, 0);
    do{
      delay(50);
    }while(digitalRead(PULSANTE) == 1);
}

///////////////////////////////// funzione wifi manager ///////////////////////////////
/**
 * @descrizione  avvio access point e la webpage  di riconfigurazione rete wifi
 * @parametro  //
 * @return     //
 */
void impostawifi(){

  int toggle_led=1;

  do{
    ledcWrite(LED_CHANNEL, 255);
    if(toggle_led==1){
      ledcWrite(LED_CHANNEL, 255);
    }
    else{
      ledcWrite(LED_CHANNEL, 0);
    }
    toggle_led=!toggle_led;

    delay(300);
  }while( misuro_pressione() <= 0); // loop infinito di blink fino a che non si rilascia il pulsante 
  
  ledcWrite(LED_CHANNEL, 255);
  
  //reset settings - wipe credentials for testing
  //wifiManager.resetSettings();
  
  // avvio l'esp in access point e resto in attesa di una configurazione della rete wifi   
  if(!wifiManager.startConfigPortal(ssid)){
      delay(2000);
      ESP.restart();
   }
   ESP.restart(); // effettua un reboot   
}

/**
 * @descrizione  effettuo 3 blink veloci e una pausa lenta 
 * @parametro  //
 * @return     //
 */
void blink_led_riconnetti(){
  
  int  conta_ciclo_led=0;
  for(conta_ciclo_led=0;conta_ciclo_led<3;conta_ciclo_led++)
  {
    ledcWrite(LED_CHANNEL, 255);
    delay(200);              // aspetta un secondo  
    ledcWrite(LED_CHANNEL, 0);  
    delay(200);   
  }
  delay(500);
}

/**
 * @descrizione  effettuo un'attesa infinita di riconnessione alla rete wifi in memoria, fino a quando la connessione non è ristabilita. 
 *               Oppure se tengo premuto il pulsante per un tempo >= TEMPO_NEW_WIFI chiamo la funzione di riconfigurazione wifi.
 * @parametro  //
 * @return     //
 */
void riconnetti() {  
    WiFi.mode(WIFI_STA);  // setto l'esp32 come client
    WiFi.begin();  
    while (WiFi.status() != WL_CONNECTED) {  // verifico che sono disconnesso 
        //delay(500); // abilitarlo nel caso si tolga "blink_led_riconnetti();"
        blink_led_riconnetti();  
        if(( anti_debounce() == 1) && (misuro_pressione() == -2))
        {
          impostawifi();
        }
    }  
}  
///////////////////////////////////////////////

void loop(){

  // controllo se sono connesso alla rete wifi 
  if(WiFi.status() != WL_CONNECTED){  
    riconnetti();    
  }
  
 else{
    
   Blynk.run();  // funzione necessaria per usare blynk
   
   if ((digitalRead(LED_PIN) == 0) && (( anti_debounce() == 1) || (start_pressione == 1)) ){
      invia_notifica();
   }
    
   if ((digitalRead(LED_PIN) == 1) && (anti_debounce() == 1) && (start_pressione == 0) ){
      spengo_notifica();
   }
  
 }
  
}
