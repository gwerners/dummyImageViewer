#include "Platform.h"
#include <cstdlib>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

std::string ToUtf8(const fs::path& p) {
  auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

fs::path FromUtf8(const std::string& s) {
  return fs::path(std::u8string(s.begin(), s.end()));
}

std::string HomeDir() {
#ifdef _WIN32
  char* v = nullptr;
  size_t n = 0;
  std::string out;
  if (_dupenv_s(&v, &n, "USERPROFILE") == 0 && v) {
    out = v;
    free(v);
  }
  return out;
#else
  const char* h = std::getenv("HOME");
  return h ? h : "";
#endif
}

std::vector<Place> GetPlaces() {
  std::vector<Place> out;
  std::string home = HomeDir();
  if (home.empty())
    return out;
  std::error_code ec;
  if (fs::is_directory(FromUtf8(home), ec))
    out.push_back({"Início", home, "home"});

  struct Cand {
    const char* kind;
    const char* label;
    const char* names[3];
  };
  const Cand cands[] = {
      {"desktop", "Área de Trabalho", {"Desktop", "Área de Trabalho", nullptr}},
      {"download", "Downloads", {"Downloads", "Transferências", nullptr}},
      {"docs", "Documentos", {"Documents", "Documentos", nullptr}},
      {"pictures", "Imagens", {"Pictures", "Imagens", nullptr}},
  };
  for (const auto& c : cands) {
    for (const char* n : c.names) {
      if (!n)
        break;
      fs::path p = FromUtf8(home) / FromUtf8(n);
      if (fs::is_directory(p, ec)) {
        out.push_back({c.label, ToUtf8(p), c.kind});
        break;
      }
    }
  }
  return out;
}

#ifdef _WIN32
std::vector<Place> GetDrives() {
  std::vector<Place> out;
  DWORD mask = GetLogicalDrives();
  for (int i = 0; i < 26; ++i) {
    if (!(mask & (1u << i)))
      continue;
    char root[4] = {(char)('A' + i), ':', '\\', 0};
    UINT type = GetDriveTypeA(root);
    if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN)
      continue;
    std::string label = std::string(root, 2);
    if (type == DRIVE_FIXED) {  // evita travar em CD/disquete vazio
      char vol[MAX_PATH] = {0};
      if (GetVolumeInformationA(root, vol, MAX_PATH, nullptr, nullptr, nullptr,
                                nullptr, 0) &&
          vol[0])
        label += std::string(" (") + vol + ")";
    } else if (type == DRIVE_REMOVABLE) {
      label += " (removível)";
    } else if (type == DRIVE_REMOTE) {
      label += " (rede)";
    } else if (type == DRIVE_CDROM) {
      label += " (CD/DVD)";
    }
    out.push_back({label, std::string(root, 3), "drive"});
  }
  return out;
}
#else
static std::string UnescapeMount(const std::string& in) {
  // /proc/mounts escapa espaco como \040
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '\\' && i + 3 < in.size() &&
        in.compare(i + 1, 3, "040") == 0) {
      out += ' ';
      i += 3;
    } else {
      out += in[i];
    }
  }
  return out;
}

std::vector<Place> GetDrives() {
  std::vector<Place> out;
  out.push_back({"Sistema (/)", "/", "drive"});
  std::ifstream m("/proc/mounts");
  std::string dev, mp, rest;
  while (m >> dev >> mp) {
    std::getline(m, rest);
    mp = UnescapeMount(mp);
    if (mp.rfind("/media/", 0) == 0 || mp.rfind("/mnt/", 0) == 0 ||
        mp.rfind("/run/media/", 0) == 0) {
      std::string label = FromUtf8(mp).filename().string();
      out.push_back({label.empty() ? mp : label, mp, "drive"});
    }
  }
  return out;
}
#endif

std::vector<unsigned char> ReadFileBytes(const std::string& utf8Path) {
  std::ifstream f(FromUtf8(utf8Path), std::ios::binary);
  if (!f)
    return {};
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),
                                    std::istreambuf_iterator<char>());
}
