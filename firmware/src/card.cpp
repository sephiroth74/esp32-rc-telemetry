#include "card.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>
#include <sdmmc_cmd.h>
#include <string.h>

#include "config.h"
#include "pins.h"

namespace {

constexpr const char* kMountPoint = "/sdcard";
constexpr const char* kProbePath = "/post_probe.bin";
constexpr size_t kChunk = LOG_WRITE_CHUNK;  // the logger's write size (§4.4)

bool mounted = false;
CardProbe lastProbe;
sdmmc_card_t rawCard;  // the card as Transfer hands it out, with no FAT volume on top
bool raw = false;

// The SDMMC host and the card on it, without SD_MMC and its FAT volume. On failure the host is
// left initialised: the caller deinitialises it either way.
bool rawInit(sdmmc_card_t& card, int freqKhz) {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = freqKhz;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = static_cast<gpio_num_t>(PIN_SD_CLK);
  slot.cmd = static_cast<gpio_num_t>(PIN_SD_CMD);
  slot.d0 = static_cast<gpio_num_t>(PIN_SD_D0);
  slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;
  if (sdmmc_host_init() != ESP_OK) return false;
  return sdmmc_host_init_slot(host.slot, &slot) == ESP_OK && sdmmc_card_init(&host, &card) == ESP_OK;
}

// Why a card that SD_MMC would not mount is unusable, from the card itself: whether it answers at
// all, and if it does, what its first sector holds. Only on the failure path, where the time spent
// does not matter.
CardMount diagnose() {
  CardMount why = CardMount::NoCard;
  sdmmc_card_t card;
  if (rawInit(card, SDMMC_FREQ_DEFAULT)) {
    why = CardMount::NoFat32;
    uint8_t* sector = static_cast<uint8_t*>(heap_caps_malloc(512, MALLOC_CAP_DMA));
    if (sector && sdmmc_read_sectors(&card, sector, 0, 1) == ESP_OK && sector[510] == 0x55 &&
        sector[511] == 0xAA) {
      uint8_t type = sector[0x1C2];  // the first partition's type in an MBR
      if (type == 0xEE) {
        why = CardMount::Gpt;  // the protective MBR in front of a GPT
      } else if (type == 0x07 || memcmp(sector + 3, "EXFAT   ", 8) == 0) {
        why = CardMount::Exfat;  // an exFAT partition, or a whole card formatted exFAT
      }
    }
    heap_caps_free(sector);
  }
  sdmmc_host_deinit();
  return why;
}

}  // namespace

CardMount cardMount() {
  if (mounted) SD_MMC.end();
  mounted = false;
  // DAT3 high before the first CMD0, or the card picks SPI mode (§2.2); it also keeps the user LED,
  // on the same line, dark.
  pinMode(PIN_SD_D3, OUTPUT);
  digitalWrite(PIN_SD_D3, HIGH);
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  if (SD_MMC.begin(kMountPoint, /*mode1bit=*/true, /*format_if_mount_failed=*/false,
                   SDMMC_FREQ_HIGHSPEED)) {
    mounted = true;
    return CardMount::Ok;
  }
  return diagnose();
}

void cardUnmount() {
  if (mounted) SD_MMC.end();
  mounted = false;
}

bool cardMounted() { return mounted; }

uint32_t cardTotalMb() { return mounted ? SD_MMC.totalBytes() / (1024 * 1024) : 0; }

uint32_t cardFreeMb() {
  return mounted ? (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024) : 0;
}

const CardProbe& cardLastProbe() { return lastProbe; }

bool cardRawBegin(uint32_t& sectors) {
  if (mounted) SD_MMC.end();
  mounted = false;
  if (raw) sdmmc_host_deinit();
  raw = rawInit(rawCard, SDMMC_FREQ_HIGHSPEED) && rawCard.csd.sector_size == 512;
  if (!raw) {
    sdmmc_host_deinit();
    return false;
  }
  sectors = rawCard.csd.capacity;
  return true;
}

bool cardRawRead(uint32_t sector, void* buf, uint32_t count) {
  return raw && sdmmc_read_sectors(&rawCard, buf, sector, count) == ESP_OK;
}

bool cardRawWrite(uint32_t sector, const void* buf, uint32_t count) {
  return raw && sdmmc_write_sectors(&rawCard, buf, sector, count) == ESP_OK;
}

void cardRawEnd() {
  if (raw) sdmmc_host_deinit();
  raw = false;
}

CardProbe cardProbe(size_t bytes) {
  CardProbe& result = lastProbe;
  result = CardProbe();
  if (!mounted) {
    result.failure = "no card";
    return result;
  }
  uint8_t* buf = static_cast<uint8_t*>(malloc(kChunk));
  if (!buf) {
    result.failure = "no memory";
    return result;
  }
  // Varies per byte, per 256-byte run and per chunk, so a misplaced sector shows up too.
  auto pattern = [](size_t chunk, size_t i) { return uint8_t(i * 31 + (i >> 8) + chunk * 7); };
  const size_t chunks = bytes / kChunk;

  File f = SD_MMC.open(kProbePath, FILE_WRITE);
  if (!f) {
    free(buf);
    result.failure = "no open";
    return result;
  }
  bool ok = true;
  uint32_t t0 = millis();
  for (size_t c = 0; c < chunks && ok; ++c) {
    for (size_t i = 0; i < kChunk; ++i) buf[i] = pattern(c, i);
    ok = f.write(buf, kChunk) == kChunk;
  }
  f.close();
  uint32_t writeMs = millis() - t0;
  if (!ok) result.failure = "short write";

  if (ok) {
    f = SD_MMC.open(kProbePath, FILE_READ);
    t0 = millis();
    for (size_t c = 0; c < chunks && ok; ++c) {
      ok = f && f.read(buf, kChunk) == kChunk;
      for (size_t i = 0; i < kChunk && ok; ++i) ok = buf[i] == pattern(c, i);
    }
    uint32_t readMs = millis() - t0;
    f.close();
    if (!ok) result.failure = "read back";
    result.readKbps = bytes / (readMs ? readMs : 1);  // bytes per millisecond are KB/s
  }
  if (!SD_MMC.remove(kProbePath) && ok) result.failure = "no delete";
  free(buf);
  result.writeKbps = bytes / (writeMs ? writeMs : 1);
  return result;
}
