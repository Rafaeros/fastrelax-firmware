#include "util/json_reader.h"

namespace json {

long readNumber(const String& body, const char* key, long fallback) {
  String needle = "\"" + String(key) + "\"";
  int keyAt = body.indexOf(needle);
  if (keyAt < 0) {
    return fallback;
  }

  int colonAt = body.indexOf(':', keyAt + needle.length());
  if (colonAt < 0) {
    return fallback;
  }

  // Pula espaços e aspas até o primeiro caractere do valor.
  int cursor = colonAt + 1;
  while (cursor < (int)body.length() &&
         (body[cursor] == ' ' || body[cursor] == '"')) {
    cursor++;
  }

  bool negative = cursor < (int)body.length() && body[cursor] == '-';
  if (negative) {
    cursor++;
  }

  long value = 0;
  bool sawDigit = false;
  while (cursor < (int)body.length() && isDigit(body[cursor])) {
    value = value * 10 + (body[cursor] - '0');
    sawDigit = true;
    cursor++;
  }

  if (!sawDigit) {
    return fallback;
  }
  return negative ? -value : value;
}

String readString(const String& body, const char* key) {
  String needle = "\"" + String(key) + "\"";
  int keyAt = body.indexOf(needle);
  if (keyAt < 0) {
    return String();
  }

  int colonAt = body.indexOf(':', keyAt + needle.length());
  if (colonAt < 0) {
    return String();
  }

  int cursor = colonAt + 1;
  while (cursor < (int)body.length() && body[cursor] == ' ') {
    cursor++;
  }

  // Sem aspas de abertura o valor nao e texto: pode ser null, numero ou objeto.
  if (cursor >= (int)body.length() || body[cursor] != '"') {
    return String();
  }
  cursor++;

  String value;
  while (cursor < (int)body.length() && body[cursor] != '"') {
    // A barra invertida nao entra no valor, e o proximo caractere entra
    // literal. Sem isto, uma senha com aspas terminaria a leitura no meio, e
    // uma com barra perderia o caractere.
    if (body[cursor] == '\\' && cursor + 1 < (int)body.length()) {
      cursor++;
    }
    value += body[cursor];
    cursor++;
  }

  return value;
}

}  // namespace json
