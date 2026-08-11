// The content layer: what gets printed, in Japanese.
//
// PrinterProtocol / PrinterDevice / EscPos know nothing about receipts. This is
// the file to replace for another layout or another language, the way ScpiPmx.hpp
// is the replaceable layer of the USBTMC example.
//
// Japanese text is stored as Shift-JIS bytes, not UTF-8.
//
// A Japanese-firmware ESC/POS printer decodes two-byte characters as Shift-JIS
// (or JIS, selected with FS C) from its own font ROM. It has no idea what UTF-8
// is, and there is no room here for a ~7000-entry conversion table, so the sample
// text is converted at authoring time and kept as byte arrays with the original
// text in the comment above each one. Arrays rather than string literals on
// purpose: a Shift-JIS second byte can be an ASCII hex digit, and "\x82" followed
// by such a character is one \x escape in C, not two bytes.
//
// For text that is not fixed at build time there are two options: keep a UTF-8 to
// Shift-JIS table for the subset you need, or render to a bitmap and print it with
// escpos::Builder::raster(), which also covers characters the font ROM lacks.

#pragma once

#include "EscPos.hpp"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace receipt
{

// A 58 mm printer is 32 half-width columns wide in font A. One two-byte character
// occupies two columns, so a line of 32 bytes is exactly full whichever mix it is,
// which is how the columns below line up.
static constexpr uint8_t COLUMNS = 32;

// ESP32 レシート印字テスト
static const uint8_t TITLE[] = {
    0x45, 0x53, 0x50, 0x33, 0x32, 0x20, 0x83, 0x8c, 0x83, 0x56, 0x81, 0x5b,
    0x83, 0x67, 0x88, 0xf3, 0x8e, 0x9a, 0x83, 0x65, 0x83, 0x58, 0x83, 0x67};

// ご来店ありがとうございます
static const uint8_t WELCOME[] = {
    0x82, 0xb2, 0x97, 0x88, 0x93, 0x58, 0x82, 0xa0, 0x82, 0xe8, 0x82, 0xaa, 0x82,
    0xc6, 0x82, 0xa4, 0x82, 0xb2, 0x82, 0xb4, 0x82, 0xa2, 0x82, 0xdc, 0x82, 0xb7};

// 商品名                数量  金額
static const uint8_t HEADER[] = {
    0x8f, 0xa4, 0x95, 0x69, 0x96, 0xbc, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x90, 0x94, 0x97, 0xca, 0x20, 0x20, 0x8b, 0xe0, 0x8a, 0x7a};

// サーマル紙 58mm         2   500
static const uint8_t ITEM1[] = {
    0x83, 0x54, 0x81, 0x5b, 0x83, 0x7d, 0x83, 0x8b, 0x8e, 0x86, 0x20, 0x35, 0x38, 0x6d, 0x6d, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x32, 0x20, 0x20, 0x20, 0x35, 0x30, 0x30};

// ESC/POS 変換ケーブル    1  1200
static const uint8_t ITEM2[] = {
    0x45, 0x53, 0x43, 0x2f, 0x50, 0x4f, 0x53, 0x20, 0x95, 0xcf, 0x8a, 0xb7, 0x83, 0x50, 0x81, 0x5b,
    0x83, 0x75, 0x83, 0x8b, 0x20, 0x20, 0x20, 0x20, 0x31, 0x20, 0x20, 0x31, 0x32, 0x30, 0x30};

// 合計                       1700
static const uint8_t TOTAL[] = {
    0x8d, 0x87, 0x8c, 0x76, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x31, 0x37, 0x30, 0x30};

// お預かり                   2000
static const uint8_t PAID[] = {
    0x82, 0xa8, 0x97, 0x61, 0x82, 0xa9, 0x82, 0xe8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x32, 0x30, 0x30, 0x30};

// おつり                      300 (CHANGE_DUE, not CHANGE: Arduino defines CHANGE as an
// interrupt mode, and a macro wins over any name a sketch chooses)
static const uint8_t CHANGE_DUE[] = {
    0x82, 0xa8, 0x82, 0xc2, 0x82, 0xe8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x33, 0x30, 0x30};

// またのご利用をお待ちしております
static const uint8_t FOOTER[] = {
    0x82, 0xdc, 0x82, 0xbd, 0x82, 0xcc, 0x82, 0xb2, 0x97, 0x98, 0x97, 0x70, 0x82, 0xf0, 0x82, 0xa8,
    0x91, 0xd2, 0x82, 0xbf, 0x82, 0xb5, 0x82, 0xc4, 0x82, 0xa8, 0x82, 0xe8, 0x82, 0xdc, 0x82, 0xb7};

inline void rule(escpos::Builder &out, char character = '-')
{
  char line[COLUMNS + 1];
  for (uint8_t i = 0; i < COLUMNS; i++)
  {
    line[i] = character;
  }
  line[COLUMNS] = '\0';
  out.line(line);
}

// Builds the whole receipt. Kanji mode is switched on for the Japanese lines and
// off again for the ASCII ones: with it on, a byte in 0x81..0x9f or 0xe0..0xef is
// taken as the first half of a two-byte character, so an ASCII line that happens
// to contain one would swallow the byte after it.
//
// cut controls only the final GS V. Everything else is identical either way, so a
// dry run on a printer without a cutter prints the same slip.
inline void buildReceipt(escpos::Builder &out, bool cut, uint32_t serial)
{
  out.init();
  out.kanjiCode(escpos::KANJI_CODE_SHIFT_JIS);
  out.codeTable(escpos::CODE_TABLE_PC437);

  out.align(escpos::ALIGN_CENTER);
  out.size(1, 2);
  out.bold(true);
  out.kanjiOn();
  out.bytes(TITLE, sizeof(TITLE));
  out.feed();
  out.kanjiOff();
  out.bold(false);
  out.size(1, 1);

  out.kanjiOn();
  out.bytes(WELCOME, sizeof(WELCOME));
  out.feed();
  out.kanjiOff();

  out.align(escpos::ALIGN_LEFT);
  rule(out, '=');
  out.kanjiOn();
  out.bytes(HEADER, sizeof(HEADER));
  out.feed();
  out.kanjiOff();
  rule(out);
  out.kanjiOn();
  out.bytes(ITEM1, sizeof(ITEM1));
  out.feed();
  out.bytes(ITEM2, sizeof(ITEM2));
  out.feed();
  out.kanjiOff();
  rule(out);
  out.kanjiOn();
  out.bold(true);
  out.bytes(TOTAL, sizeof(TOTAL));
  out.feed();
  out.bold(false);
  out.bytes(PAID, sizeof(PAID));
  out.feed();
  out.bytes(CHANGE_DUE, sizeof(CHANGE_DUE));
  out.feed();
  out.kanjiOff();
  rule(out, '=');

  // A CODE128 payload starts with its own code-set selector: "{B" is the
  // alphanumeric set. Printed left to right, the digits below it are the printer's
  // own HRI text (GS H 2).
  out.align(escpos::ALIGN_CENTER);
  out.barcodeHeight(60);
  out.barcodeWidth(2);
  out.barcodeTextPosition(2);
  char code[16];
  snprintf(code, sizeof(code), "{B%08lu", static_cast<unsigned long>(serial));
  out.barcode(73, reinterpret_cast<const uint8_t *>(code), strlen(code));
  out.feed();

  out.qr("https://github.com/tanakamasayuki/EspUsbHost", 4);
  out.feed();

  out.kanjiOn();
  out.bytes(FOOTER, sizeof(FOOTER));
  out.feed();
  out.kanjiOff();

  out.align(escpos::ALIGN_LEFT);
  if (cut)
  {
    // Feed before cutting: without it the last lines are still inside the
    // mechanism, above the blade.
    out.cut(escpos::CUT_FEED_AND_PARTIAL, 0x50);
  }
  else
  {
    out.feedLines(4);
  }
}

// A short ASCII-only slip. Useful as the first thing to send to an unknown
// printer: if this prints and the receipt above does not, the problem is the
// two-byte font, not the transport.
inline void buildAsciiTest(escpos::Builder &out, bool cut)
{
  out.init();
  out.align(escpos::ALIGN_CENTER);
  out.bold(true);
  out.line("EspUsbHost ESC/POS");
  out.bold(false);
  out.align(escpos::ALIGN_LEFT);
  rule(out);
  out.line("plain text");
  out.bold(true);
  out.line("bold");
  out.bold(false);
  out.underline(1);
  out.line("underline");
  out.underline(0);
  out.inverse(true);
  out.line("inverse");
  out.inverse(false);
  out.size(2, 2);
  out.line("double");
  out.size(1, 1);
  out.align(escpos::ALIGN_RIGHT);
  out.line("right");
  out.align(escpos::ALIGN_CENTER);
  out.line("center");
  out.align(escpos::ALIGN_LEFT);
  rule(out);
  if (cut)
  {
    out.cut(escpos::CUT_FEED_AND_PARTIAL, 0x50);
  }
  else
  {
    out.feedLines(4);
  }
}

} // namespace receipt
