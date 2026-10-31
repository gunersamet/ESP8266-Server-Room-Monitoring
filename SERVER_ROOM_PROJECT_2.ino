#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <UniversalTelegramBot.h>




#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>
#include <DHT.h>
#define MQ2_PIN D6
#define BUZZER_PIN D7

//=========================
// LED'ler
//=========================
#define GREEN_LED_PIN D0   // Normal durum - sabit yeşil
#define RED_LED_PIN D8     // Alarm durumu - kırmızı

//=========================
// WiFi Bilgileri
//=========================
const char* ssid = "GURALP_INT";
const char* password = "cZjpD#R&x35";

//=========================
// Telegram
//=========================
#define BOT_TOKEN "8974562631:AAFp8_IYJSgRM4bVQ9tILov-33uUBf1tLjQ"
#define CHAT_ID "5412357055"
#define CHAT_ID_2 "5679059718"

//=========================
// LCD
//=========================
LiquidCrystal_I2C lcd(0x27, 16, 2);


//=========================
// DHT11
//=========================
#define DHTPIN D5
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

//=========================
// Telegram Client
//=========================
std::unique_ptr<BearSSL::WiFiClientSecure> client;
UniversalTelegramBot* bot;
bool sicaklikAlarmiAktif = false;
bool nemAlarmiAktif = false;
bool gazAlarmiAktif = false;
bool buzzerSusturuldu = false;
//=========================

//=========================
// Sicaklik/Nem filtreleme (hatali ani dususleri elemek icin)
//=========================
float sonGecerliSicaklik = NAN;
float sonGecerliNem = NAN;
const float MAX_DUSUS = 5.0;      // izin verilen maksimum ani dusus (derece)
const int MAX_DENEME = 5;         // kac kere tekrar okumaya calisilacak
const int DENEME_ARASI_MS = 500;  // deneme araligi

// WiFi durum kodunu okunabilir metne çeviren yardımcı fonksiyon (debug için)
String wifiStatusMetni(wl_status_t durum) {
  switch (durum) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "SSID BULUNAMADI";
    case WL_SCAN_COMPLETED: return "TARAMA TAMAMLANDI";
    case WL_CONNECT_FAILED: return "BAGLANTI HATASI (sifre yanlis olabilir)";
    case WL_CONNECTION_LOST: return "BAGLANTI KOPTU";
    case WL_DISCONNECTED: return "BAGLI DEGIL";
    default: return "BILINMIYOR (" + String((int)durum) + ")";
  }
}

// DHT'den guvenilir sicaklik/nem okur; sadece ani DUSUSLERI eler (artis serbest), tekrar dener
bool guvenilirOku(float &sicaklikOut, float &nemOut) {
  for (int deneme = 0; deneme < MAX_DENEME; deneme++) {
    float s = dht.readTemperature();
    float n = dht.readHumidity();

    if (isnan(s) || isnan(n)) {
      delay(DENEME_ARASI_MS);
      continue;
    }

    // Ilk okuma ise veya daha once gecerli deger yoksa direkt kabul et
    if (isnan(sonGecerliSicaklik)) {
      sonGecerliSicaklik = s;
      sonGecerliNem = n;
      sicaklikOut = s;
      nemOut = n;
      return true;
    }

    // Sadece DUSUS kontrolu yapiyoruz, artis serbest birakiliyor
    float dususMiktari = sonGecerliSicaklik - s;

    if (dususMiktari > MAX_DUSUS) {
      // Anlamsiz ani dusus -> bu okumayi at, tekrar dene
      Serial.print("Supheli sicaklik dususu elendi: ");
      Serial.println(s);
      delay(DENEME_ARASI_MS);
      continue;
    }

    // Gecerli okuma
    sonGecerliSicaklik = s;
    sonGecerliNem = n;
    sicaklikOut = s;
    nemOut = n;
    return true;
  }

  // MAX_DENEME sonunda hala guvenilir okuma alinamadiysa
  return false;
}

void setup() {

  Serial.begin(115200);
  Wire.begin(D2, D1);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Server Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Baslatiliyor");
  pinMode(MQ2_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // LED pinleri
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, HIGH); // Başlangıçta normal kabul ediyoruz
  digitalWrite(RED_LED_PIN, LOW);


  // DHT11
  dht.begin();

  // WiFi
  //=========================
  // WiFi
  //=========================
  // ESP8266 onceki sketch'ten kalma AP modunda takili kalabiliyor,
  // bu yuzden once acikca STA moduna geciyoruz ve eski baglantiyi temizliyoruz.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  WiFi.begin(ssid, password);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Baglaniyor");

  unsigned long baslangic = millis();
  // Timeout 10sn -> 20sn'ye cikarildi, bazi aglarda ilk baglanti daha uzun surebiliyor
  const unsigned long WIFI_TIMEOUT = 20000;

  while (WiFi.status() != WL_CONNECTED && millis() - baslangic < WIFI_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  lcd.clear();

  if (WiFi.status() == WL_CONNECTED) {
    configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    Serial.println();
    Serial.println("WiFi Baglandi");
    Serial.println(WiFi.localIP());
    // 1 saniyelik başlangıç sesi
    digitalWrite(BUZZER_PIN, HIGH);
    delay(1000);
    digitalWrite(BUZZER_PIN, LOW);

    lcd.setCursor(0, 0);
    lcd.print("WiFi OK");

    client.reset(new BearSSL::WiFiClientSecure);
    client->setInsecure();

    bot = new UniversalTelegramBot(BOT_TOKEN, *client);

    if (bot->sendMessage(CHAT_ID, "✅ Sistem Başlatıldı /help komutunu kullanabilirsiniz!", "")) {

      lcd.setCursor(0, 1);
      lcd.print("Telegram OK");

      // İkinci kişiye de gönder
      bot->sendMessage(CHAT_ID_2, "✅ Sistem Başlatıldı /help komutunu kullanabilirsiniz! ", "");

    } else {

      lcd.setCursor(0, 1);
      lcd.print("Telegram HATA");
    }

  } else {
    // Debug icin: neden baglanamadigini Serial'e yazdiriyoruz
    Serial.println();
    Serial.print("WiFi Baglanamadi. Sebep: ");
    Serial.println(wifiStatusMetni(WiFi.status()));

    lcd.setCursor(0, 0);
    lcd.print("WiFi Yok");
    lcd.setCursor(0, 1);
    lcd.print("Offline Mod");
  }

  delay(2000);
}

void telegramMesajlariniIsle(int mesajSayisi) {

  for (int i = 0; i < mesajSayisi; i++) {

    String text = bot->messages[i].text;
    String chat_id = bot->messages[i].chat_id;

    text.toLowerCase();

    if (text == "/durum") {

      // Tarih Saat
      time_t now = time(nullptr);
      struct tm* timeinfo = localtime(&now);

      char tarihSaat[30];
      sprintf(
        tarihSaat,
        "%02d.%02d.%04d %02d:%02d:%02d",
        timeinfo->tm_mday,
        timeinfo->tm_mon + 1,
        timeinfo->tm_year + 1900,
        timeinfo->tm_hour,
        timeinfo->tm_min,
        timeinfo->tm_sec);

      // DHT Oku (guvenilir, filtrelenmis okuma)
      float sicaklik, nem;
      bool okumaBasarili = guvenilirOku(sicaklik, nem);
      int gazDurumu = digitalRead(MQ2_PIN);
      int gazSeviyesi = analogRead(A0);

      if (!okumaBasarili) {

        bot->sendMessage(chat_id, "❌ DHT22 okunamadi!", "");

      } else {

        String mesaj = "";

        mesaj += "🖥️ SERVER ROOM MONITOR\n\n";
        mesaj += "🟢 Durum : ONLINE\n\n";

        mesaj += "📅 Tarih/Saat : ";
        mesaj += tarihSaat;
        mesaj += "\n\n";

        mesaj += "🌡️ Sicaklik : ";
        mesaj += String(sicaklik, 1);
        mesaj += " °C\n\n";

        mesaj += "💧 Nem : %";
        mesaj += String(nem, 0);
        mesaj += "\n\n";

        mesaj += "🔥 Gaz Durumu : ";
        if (gazDurumu == LOW) {
          mesaj += "🚨 GAZ ALGILANDI!";
        } else {
          mesaj += "✅ Temiz";
        }

        bot->sendMessage(chat_id, mesaj, "");
      }

    } else if (text == "/mute") {

      buzzerSusturuldu = true;
      bot->sendMessage(chat_id,
                       "🔇 Alarm sesi susturuldu.\nAlarm devam ediyor ancak buzzer kapatıldı.", "");

    } else if (text == "/unmute") {

      buzzerSusturuldu = false;
      bot->sendMessage(chat_id, "🔊 Alarm sesi tekrar aktif edildi.", "");

    } else if (text == "/help") {

      String mesaj = "";
      mesaj += "⚙️ SERVER ROOM MONITOR BOT\n\n";
      mesaj += "Kullanılabilir Komutlar:\n\n";
      mesaj += "📊 /durum\n";
      mesaj += "Anlık sıcaklık, nem ve gaz durumunu gösterir.\n\n";
      mesaj += "🔇 /mute\n";
      mesaj += "Alarm sesi (buzzer) susturulur.\n\n";
      mesaj += "🔊 /unmute\n";
      mesaj += "Alarm sesi tekrar aktif edilir.\n\n";
      mesaj += "❓ /help\n";
      mesaj += "Komut listesini gösterir.";

      bot->sendMessage(chat_id, mesaj, "");
    }
  }
}

void loop() {
  int duman = digitalRead(MQ2_PIN);


  if (duman == LOW) {

    Serial.println("DUMAN ALGILANDI");

  } else {

    Serial.println("Temiz Hava");
  }

  // Buradan sonra LCD kodların devam edecek...
  // DHT'den guvenilir (filtrelenmis) okuma
  float sicaklik, nem;
  bool okumaBasarili = guvenilirOku(sicaklik, nem);
  if (!okumaBasarili) {
    sicaklik = NAN;
    nem = NAN;
  }
  //=========================
  // SICAKLIK KONTROLÜ
  //=========================

  if (WiFi.status() == WL_CONNECTED && bot != nullptr) {
    if ((sicaklik < 17.5 || sicaklik > 27) && !sicaklikAlarmiAktif) {
      String mesaj = "🌡️ SICAKLIK UYARISI!\n\n";
      mesaj += "Server Odasının sıcaklığı normal aralığın dışında!🚨🚨\n\n";
      mesaj += "Ölçülen Sıcaklık: ";
      mesaj += String(sicaklik, 1);
      mesaj += " °C\n\n";
      mesaj += "Normal Aralık: 18°C - 27°C";

      bot->sendMessage(CHAT_ID, mesaj, "");
      bot->sendMessage(CHAT_ID_2, mesaj, "");

      sicaklikAlarmiAktif = true;
    }

    else if (sicaklik >= 17.5 && sicaklik <= 27 && sicaklikAlarmiAktif) {
      String mesaj = "✅ SICAKLIK NORMALE DÖNDÜ\n\n";
      mesaj += "Anlık Sıcaklık: ";
      mesaj += String(sicaklik, 1);
      mesaj += " °C";

      bot->sendMessage(CHAT_ID, mesaj, "");
      bot->sendMessage(CHAT_ID_2, mesaj, "");

      sicaklikAlarmiAktif = false;
    }
    //=========================
    // NEM KONTROLÜ
    //=========================

    if ((nem < 40 || nem > 66) && !nemAlarmiAktif) {
      String mesaj = "💧 NEM UYARISI!\n\n";
      mesaj += "Server Odasının nemi normal aralığın dışında!🚨🚨\n\n";
      mesaj += "Ölçülen Nem: %";
      mesaj += String(nem, 0);
      mesaj += "\n";
      mesaj += "Normal Aralık:%30 - %66";

      bot->sendMessage(CHAT_ID, mesaj, "");
      bot->sendMessage(CHAT_ID_2, mesaj, "");

      nemAlarmiAktif = true;
    }

    else if (nem >= 40 && nem <= 66 && nemAlarmiAktif) {
      String mesaj = "✅ Nem Normale Döndü\n\n";
      mesaj += "Anlık Nem: %";
      mesaj += String(nem, 0);

      bot->sendMessage(CHAT_ID, mesaj, "");
      bot->sendMessage(CHAT_ID_2, mesaj, "");

      nemAlarmiAktif = false;
    }
    //=========================
    // GAZ KONTROLÜ
    //=========================

    if (duman == LOW && !gazAlarmiAktif) {
      String mesaj = "🔥 GAZ UYARISI!\n\n";
      mesaj += "Server Odasında gaz/duman algılandı!🚨🚨\n\n";
      mesaj += "Lütfen ortamı kontrol edin.";

      bot->sendMessage(CHAT_ID, mesaj, "");
      bot->sendMessage(CHAT_ID_2, mesaj, "");

      gazAlarmiAktif = true;
    }

    else if (duman == HIGH && gazAlarmiAktif) {
      String mesaj = "✅ GAZ NORMALE DÖNDÜ\n\n";
      mesaj += "Gaz seviyesi tekrar normal.";

      bot->sendMessage(CHAT_ID, mesaj, "");
      bot->sendMessage(CHAT_ID_2, mesaj, "");

      gazAlarmiAktif = false;
    }

    if ((sicaklikAlarmiAktif || nemAlarmiAktif || gazAlarmiAktif) && !buzzerSusturuldu) {
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }

    // LED durumu: herhangi bir alarm aktifse kırmızı, değilse yeşil
    bool alarmVarMi = sicaklikAlarmiAktif || nemAlarmiAktif || gazAlarmiAktif;
    if (alarmVarMi) {
      digitalWrite(RED_LED_PIN, HIGH);
      digitalWrite(GREEN_LED_PIN, LOW);
    } else {
      digitalWrite(RED_LED_PIN, LOW);
      digitalWrite(GREEN_LED_PIN, HIGH);
    }
  }


  if (WiFi.status() == WL_CONNECTED && bot != nullptr) {

    int yeniMesaj = bot->getUpdates(bot->last_message_received + 1);

    while (yeniMesaj) {
      telegramMesajlariniIsle(yeniMesaj);
      yeniMesaj = bot->getUpdates(bot->last_message_received + 1);
    }
  }

  if (isnan(sicaklik) || isnan(nem)) {

    Serial.println("DHT22 okunamadı!");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DHT22 HATASI");

    delay(2000);
    return;
  }

  Serial.print("Sıcaklık: ");
  Serial.print(sicaklik);
  Serial.print(" C   ");

  Serial.print("Nem: ");
  Serial.print(nem);
  Serial.println(" %");


  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("SICAKLIK:");
  lcd.print(sicaklik, 1);
  lcd.print((char)223);
  lcd.print("C");

  lcd.setCursor(0, 1);
  lcd.print("NEM:");
  lcd.print(nem, 0);
  lcd.print("%");

  delay(2000);
}
