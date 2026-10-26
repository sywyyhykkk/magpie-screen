#pragma once
#include <cctype>
#include <cstring>

namespace appearance {
enum class Brand { Unknown, Codex, Claude, Gemini, Cursor };

inline bool equalsIgnoreCase(const char *a, const char *b) {
  while (*a && *b) {
    if (std::tolower(static_cast<unsigned char>(*a++)) !=
        std::tolower(static_cast<unsigned char>(*b++)))
      return false;
  }
  return *a == *b;
}

inline Brand brand(const char *provider) {
  // Remote providers retain their service name as the final path component.
  const char *tail = std::strrchr(provider, '/');
  const char *name = tail ? tail + 1 : provider;
  if (equalsIgnoreCase(name, "claude") || equalsIgnoreCase(name, "anthropic"))
    return Brand::Claude;
  if (equalsIgnoreCase(name, "codex") || equalsIgnoreCase(name, "openai") ||
      equalsIgnoreCase(name, "chatgpt"))
    return Brand::Codex;
  if (equalsIgnoreCase(name, "gemini") || equalsIgnoreCase(name, "google") ||
      equalsIgnoreCase(name, "gemini-cli") || equalsIgnoreCase(name, "geminicli"))
    return Brand::Gemini;
  if (equalsIgnoreCase(name, "cursor"))
    return Brand::Cursor;
  return Brand::Unknown;
}

inline bool validChoice(const char *choice) {
  return std::strcmp(choice, "auto") == 0 || std::strcmp(choice, "codex") == 0 ||
         std::strcmp(choice, "claude") == 0 || std::strcmp(choice, "gemini") == 0;
}

inline Brand resolve(const char *choice, const char *provider) {
  return brand(std::strcmp(choice, "auto") == 0 ? provider : choice);
}

inline Brand liquidTheme(const char *choice, const char *provider) {
  Brand selected = resolve(choice, provider);
  return selected == Brand::Claude || selected == Brand::Gemini ? selected : Brand::Codex;
}
} // namespace appearance
