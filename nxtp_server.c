/*
 * ============================================================
 * NXTP Protocol Version 1 Server
 *
 * Platform: Raspberry Pi OS / Linux
 * Language: C
 * Default TCP port: 12300
 *
 * Protocol:
 *   Request:
 *     [Version] [TimezoneLength] [Timezone] [Checksum]
 *
 *   Response:
 *     [Version] [DateLength] [TimeLength]
 *     [Date: dd/MM/yyyy]
 *     [Time: HH:mm:ss]
 *     [Checksum]
 *
 * Checksum:
 *   XOR of all packet bytes, starting with seed 123.
 *
 * Build:
 *   gcc -Wall -Wextra -O2 -o nxtp_server nxtp_server.c
 *
 * Run:
 *   sudo ./nxtp_server
 *
 * Run on another port:
 *   ./nxtp_server 12300
 * ============================================================
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define NXTP_VERSION       1
#define NXTP_CHECKSUM_SEED 123
#define NXTP_MAX_TZ_LEN    60
#define NXTP_DATE_LEN      10
#define NXTP_TIME_LEN      8
#define NXTP_PORT          12300
#define NXTP_BACKLOG       16
#define NXTP_IO_TIMEOUT    5

static volatile sig_atomic_t running = 1;

#define LOG(...)                  \
  do {                            \
    fprintf(stdout, __VA_ARGS__); \
    fputc('\n', stdout);          \
    fflush(stdout);               \
  } while (0)

/*
 * Windows/NXTP timezone code to Linux IANA timezone.
 *
 * Add additional mappings here as required.
 */

typedef struct {
    const char *nxtp_code;
    const char *iana_tz;
} timezone_entry_t;

/* NXTP timezone codes from the official wiki list, mapped to IANA tzdata names. */

static const timezone_entry_t timezone_map[] = {
    {"DatelineStandardTime", "Etc/GMT+12"},
    {"UTC-11", "Etc/GMT+11"},
    {"AleutianStandardTime", "America/Adak"},
    {"HawaiianStandardTime", "Pacific/Honolulu"},
    {"MarquesasStandardTime", "Pacific/Marquesas"},
    {"AlaskanStandardTime", "America/Anchorage"},
    {"UTC-09", "Etc/GMT+9"},
    {"PacificStandardTime(Mexico)", "America/Tijuana"},
    {"UTC-08", "Etc/GMT+8"},
    {"PacificStandardTime", "America/Los_Angeles"},
    {"USMountainStandardTime", "America/Phoenix"},
    {"MountainStandardTime(Mexico)", "America/Chihuahua"},
    {"MountainStandardTime", "America/Denver"},
    {"CentralAmericaStandardTime", "America/Guatemala"},
    {"CentralStandardTime", "America/Chicago"},
    {"EasterIslandStandardTime", "Pacific/Easter"},
    {"CentralStandardTime(Mexico)", "America/Mexico_City"},
    {"CanadaCentralStandardTime", "America/Regina"},
    {"SAPacificStandardTime", "America/Bogota"},
    {"EasternStandardTime(Mexico)", "America/Cancun"},
    {"EasternStandardTime", "America/New_York"},
    {"EST", "America/New_York"},
    {"HaitiStandardTime", "America/Port-au-Prince"},
    {"CubaStandardTime", "America/Havana"},
    {"USEasternStandardTime", "America/Indiana/Indianapolis"},
    {"TurksAndCaicosStandardTime", "America/Grand_Turk"},
    {"ParaguayStandardTime", "America/Asuncion"},
    {"AtlanticStandardTime", "America/Halifax"},
    {"VenezuelaStandardTime", "America/Caracas"},
    {"CentralBrazilianStandardTime", "America/Cuiaba"},
    {"SAWesternStandardTime", "America/La_Paz"},
    {"PacificSAStandardTime", "America/Santiago"},
    {"NewfoundlandStandardTime", "America/St_Johns"},
    {"TocantinsStandardTime", "America/Araguaina"},
    {"E.SouthAmericaStandardTime", "America/Sao_Paulo"},
    {"SAEasternStandardTime", "America/Cayenne"},
    {"ArgentinaStandardTime", "America/Argentina/Buenos_Aires"},
    {"GreenlandStandardTime", "America/Nuuk"},
    {"MontevideoStandardTime", "America/Montevideo"},
    {"MagallanesStandardTime", "America/Punta_Arenas"},
    {"SaintPierreStandardTime", "America/Miquelon"},
    {"BahiaStandardTime", "America/Bahia"},
    {"UTC-02", "Etc/GMT+2"},
    {"Mid-AtlanticStandardTime", "Etc/GMT+2"},
    {"AzoresStandardTime", "Atlantic/Azores"},
    {"CapeVerdeStandardTime", "Atlantic/Cape_Verde"},
    {"UTC", "UTC"},
    {"GMT", "Europe/London"},
    {"GMTStandardTime", "Europe/London"},
    {"GreenwichStandardTime", "Africa/Monrovia"},
    {"W.EuropeStandardTime", "Europe/Berlin"},
    {"CentralEuropeStandardTime", "Europe/Budapest"},
    {"CET", "Europe/Belgrade"},
    {"RomanceStandardTime", "Europe/Paris"},
    {"MoroccoStandardTime", "Africa/Casablanca"},
    {"SaoTomeStandardTime", "Africa/Sao_Tome"},
    {"CentralEuropeanStandardTime", "Europe/Warsaw"},
    {"W.CentralAfricaStandardTime", "Africa/Lagos"},
    {"JordanStandardTime", "Asia/Amman"},
    {"GTBStandardTime", "Europe/Athens"},
    {"MiddleEastStandardTime", "Asia/Beirut"},
    {"EgyptStandardTime", "Africa/Cairo"},
    {"E.EuropeStandardTime", "Europe/Chisinau"},
    {"SyriaStandardTime", "Asia/Damascus"},
    {"WestBankStandardTime", "Asia/Hebron"},
    {"SouthAfricaStandardTime", "Africa/Johannesburg"},
    {"FLEStandardTime", "Europe/Helsinki"},
    {"IsraelStandardTime", "Asia/Jerusalem"},
    {"KaliningradStandardTime", "Europe/Kaliningrad"},
    {"SudanStandardTime", "Africa/Khartoum"},
    {"LibyaStandardTime", "Africa/Tripoli"},
    {"NamibiaStandardTime", "Africa/Windhoek"},
    {"ArabicStandardTime", "Asia/Baghdad"},
    {"TurkeyStandardTime", "Europe/Istanbul"},
    {"ArabStandardTime", "Asia/Riyadh"},
    {"BelarusStandardTime", "Europe/Minsk"},
    {"RussianStandardTime", "Europe/Moscow"},
    {"E.AfricaStandardTime", "Africa/Nairobi"},
    {"IranStandardTime", "Asia/Tehran"},
    {"ArabianStandardTime", "Asia/Dubai"},
    {"AstrakhanStandardTime", "Europe/Astrakhan"},
    {"AzerbaijanStandardTime", "Asia/Baku"},
    {"RussiaTimeZone3", "Europe/Samara"},
    {"MauritiusStandardTime", "Indian/Mauritius"},
    {"SaratovStandardTime", "Europe/Saratov"},
    {"GeorgianStandardTime", "Asia/Tbilisi"},
    {"VolgogradStandardTime", "Europe/Volgograd"},
    {"CaucasusStandardTime", "Asia/Yerevan"},
    {"AfghanistanStandardTime", "Asia/Kabul"},
    {"WestAsiaStandardTime", "Asia/Tashkent"},
    {"EkaterinburgStandardTime", "Asia/Yekaterinburg"},
    {"PakistanStandardTime", "Asia/Karachi"},
    {"IndiaStandardTime", "Asia/Kolkata"},
    {"SriLankaStandardTime", "Asia/Colombo"},
    {"NepalStandardTime", "Asia/Kathmandu"},
    {"CentralAsiaStandardTime", "Asia/Almaty"},
    {"BangladeshStandardTime", "Asia/Dhaka"},
    {"OmskStandardTime", "Asia/Omsk"},
    {"MyanmarStandardTime", "Asia/Yangon"},
    {"SEAsiaStandardTime", "Asia/Bangkok"},
    {"AltaiStandardTime", "Asia/Barnaul"},
    {"W.MongoliaStandardTime", "Asia/Hovd"},
    {"NorthAsiaStandardTime", "Asia/Krasnoyarsk"},
    {"N.CentralAsiaStandardTime", "Asia/Novosibirsk"},
    {"TomskStandardTime", "Asia/Tomsk"},
    {"ChinaStandardTime", "Asia/Shanghai"},
    {"NorthAsiaEastStandardTime", "Asia/Irkutsk"},
    {"SingaporeStandardTime", "Asia/Singapore"},
    {"W.AustraliaStandardTime", "Australia/Perth"},
    {"TaipeiStandardTime", "Asia/Taipei"},
    {"UlaanbaatarStandardTime", "Asia/Ulaanbaatar"},
    {"AusCentralW.StandardTime", "Australia/Eucla"},
    {"TransbaikalStandardTime", "Asia/Chita"},
    {"TokyoStandardTime", "Asia/Tokyo"},
    {"NorthKoreaStandardTime", "Asia/Pyongyang"},
    {"KoreaStandardTime", "Asia/Seoul"},
    {"YakutskStandardTime", "Asia/Yakutsk"},
    {"Cen.AustraliaStandardTime", "Australia/Adelaide"},
    {"AUSCentralStandardTime", "Australia/Darwin"},
    {"E.AustraliaStandardTime", "Australia/Brisbane"},
    {"AUSEasternStandardTime", "Australia/Sydney"},
    {"WestPacificStandardTime", "Pacific/Port_Moresby"},
    {"TasmaniaStandardTime", "Australia/Hobart"},
    {"VladivostokStandardTime", "Asia/Vladivostok"},
    {"LordHoweStandardTime", "Australia/Lord_Howe"},
    {"BougainvilleStandardTime", "Pacific/Bougainville"},
    {"RussiaTimeZone10", "Asia/Srednekolymsk"},
    {"MagadanStandardTime", "Asia/Magadan"},
    {"NorfolkStandardTime", "Pacific/Norfolk"},
    {"SakhalinStandardTime", "Asia/Sakhalin"},
    {"CentralPacificStandardTime", "Pacific/Guadalcanal"},
    {"RussiaTimeZone11", "Asia/Anadyr"},
    {"NewZealandStandardTime", "Pacific/Auckland"},
    {"UTC+12", "Etc/GMT-12"},
    {"FijiStandardTime", "Pacific/Fiji"},
    {"KamchatkaStandardTime", "Asia/Kamchatka"},
    {"ChathamIslandsStandardTime", "Pacific/Chatham"},
    {"UTC+13", "Etc/GMT-13"},
    {"TongaStandardTime", "Pacific/Tongatapu"},
    {"SamoaStandardTime", "Pacific/Apia"},
    {"LineIslandsStandardTime", "Pacific/Kiritimati"}};

#define TIMEZONE_MAP_COUNT \
    (sizeof(timezone_map) / sizeof(timezone_map[0]))

/* ------------------------------------------------------------
 * Signal handling
 * ------------------------------------------------------------ */

static volatile sig_atomic_t stopping = 0;

static void on_signal(int sig) {
  (void)sig;
  stopping = 1;
}

static void reap_children(int sig) {
  (void)sig;
  while (waitpid(-1, NULL, WNOHANG) > 0) {
  }
}

/* ------------------------------------------------------------
 * Calculate NXTP XOR checksum
 * ------------------------------------------------------------ */
static uint8_t nxtp_checksum(const uint8_t *data, size_t length) {
    uint8_t checksum = NXTP_CHECKSUM_SEED;

    for (size_t i = 0; i < length; i++)
        checksum ^= data[i];

    return checksum;
}

/* ------------------------------------------------------------
 * Translate timezone code to IANA timezone
 *
 * Empty timezone means GMT.
 * ------------------------------------------------------------ */
static const char *lookup_zone(const char *nxtp_code) {
  if (nxtp_code == NULL || nxtp_code[0] == '\0')
    return "Europe/London";

  for (size_t i = 0; i < TIMEZONE_MAP_COUNT; i++)
    if (!strcasecmp(nxtp_code, timezone_map[i].nxtp_code))
		return timezone_map[i].iana_tz;

  return NULL;
}

/* ------------------------------------------------------------
 * Receive exact number of bytes
 * ------------------------------------------------------------ */

static int receive_all(int fd, uint8_t *buffer, size_t length) {
    size_t received = 0;

    while (received < length) {
        ssize_t n = recv(fd,
                         buffer + received,
                         length - received,
                         0);

        if (n == 0)
            return 0;

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        received += (size_t)n;
    }

    return 1;
}

/* ------------------------------------------------------------
 * Send exact number of bytes
 * ------------------------------------------------------------ */
static int send_all(int fd, const uint8_t *buffer, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = send(fd,
                         buffer + sent,
                         length - sent,
                         0);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (n == 0)
            return -1;

        sent += (size_t)n;
    }

    return 0;
}

/* ------------------------------------------------------------
 * Process one client connection
 * ------------------------------------------------------------ */
static void serve_client(int fd, const char *ip) {
  uint8_t req[2 + NXTP_MAX_TZ_LEN + 1], resp[22];
  struct timeval tv = {NXTP_IO_TIMEOUT, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  int rr = receive_all(fd, req, 2);
  if (rr != 1) {
    LOG("%s: incomplete header", ip);
    return;
  }
  if (req[0] != 1) {
    LOG("%s: unsupported protocol version %u", ip, req[0]);
    return;
  }
  unsigned len = req[1];
  if (len > NXTP_MAX_TZ_LEN) {
    LOG("%s: invalid timezone length %u", ip, len);
    return;
  }
  if (receive_all(fd, req + 2, len + 1) != 1) {
    LOG("%s: incomplete request", ip);
    return;
  }
  size_t plen = 2 + len + 1;
  if (nxtp_checksum(req, plen - 1) != req[plen - 1]) {
    LOG("%s: bad request checksum", ip);
    return;
  }
  char nxtp_code[NXTP_MAX_TZ_LEN + 1];
  memcpy(nxtp_code, req + 2, len);
  nxtp_code[len] = '\0';
  for (unsigned i = 0; i < len; i++)
    if ((unsigned char)nxtp_code[i] < 33 || (unsigned char)nxtp_code[i] > 126) {
      LOG("%s: invalid timezone characters", ip);
      return;
    }
  const char *zone = lookup_zone(nxtp_code);
  if (!zone) {
    LOG("%s: unsupported timezone '%s'", ip, nxtp_code);
    return;
  }
  if (setenv("TZ", zone, 1) < 0) {
    LOG("%s: setenv failed: %s", ip, strerror(errno));
    return;
  }
  tzset();
  time_t now = time(NULL);
  struct tm tmv;
  if (!localtime_r(&now, &tmv)) {
    LOG("%s: localtime failed", ip);
    return;
  }
  resp[0] = 1;
  resp[1] = 10;
  resp[2] = 8;
  if (strftime((char *)resp + 3, 11, "%d/%m/%Y", &tmv) != 10 ||
      strftime((char *)resp + 13, 9, "%H:%M:%S", &tmv) != 8) {
    LOG("%s: date formatting failed", ip);
    return;
  }
  resp[21] = nxtp_checksum(resp, 21);
  if (send_all(fd, resp, sizeof resp) < 0) {
    LOG("%s: send failed: %s", ip, strerror(errno));
    return;
  }
  LOG("%s: OK timezone=%s (%s), date=%.*s time=%.*s", ip,
      len ? nxtp_code : "(default GMT)", zone, 10, resp + 3, 8, resp + 13);
}

/* ------------------------------------------------------------
 * Main server
 * ------------------------------------------------------------ */
int main(int argc, char **argv) {
  char *end = NULL;
  long port = NXTP_PORT;
  if (argc > 2) {
    fprintf(stderr, "Usage: %s [port]\n", argv[0]);
    return 2;
  }
  if (argc == 2) {
    errno = 0;
    port = strtol(argv[1], &end, 10);
    if (errno || !end || *end || port < 1 || port > 65535) {
      fprintf(stderr, "Invalid port\n");
      return 2;
    }
  }
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  signal(SIGCHLD, reap_children);
  signal(SIGPIPE, SIG_IGN);
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    perror("socket");
    return 1;
  }
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((uint16_t)port);
  if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) {
    perror("bind");
    close(s);
    return 1;
  }
  if (listen(s, NXTP_BACKLOG) < 0) {
    perror("listen");
    close(s);
    return 1;
  }
  LOG("NXTP v1 server listening on TCP port %ld", port);
  while (!stopping) {
    struct sockaddr_in ca;
    socklen_t calen = sizeof ca;
    int c = accept(s, (struct sockaddr *)&ca, &calen);
    if (c < 0) {
      if (errno == EINTR) continue;
      if (stopping) break;
      LOG("accept: %s", strerror(errno));
      continue;
    }
    char ip[INET_ADDRSTRLEN] = "unknown";
    inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof ip);
    pid_t pid = fork();
    if (pid < 0) {
      LOG("fork for %s failed: %s", ip, strerror(errno));
      close(c);
      continue;
    }
    if (pid == 0) {
      close(s);
      serve_client(c, ip);
      shutdown(c, SHUT_RDWR);
      close(c);
      _exit(0);
    }
    close(c);
    LOG("Accepted client %s (pid %ld)", ip, (long)pid);
  }
  close(s);
  while (waitpid(-1, NULL, 0) > 0) {
  }
  LOG("NXTP server stopped");
  return 0;
}
