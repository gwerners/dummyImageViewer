#pragma once
// Camada de plataforma (Linux / Windows). NAO inclui raylib: no Windows o
// <windows.h> colide com nomes do raylib (Rectangle, CloseWindow, ...).
#include <filesystem>
#include <string>
#include <vector>

struct Place {
  std::string label;  // texto mostrado
  std::string path;   // caminho em UTF-8
  std::string kind;   // home, desktop, download, docs, pictures, drive
};

// Conversao segura de caminhos <-> UTF-8 (no Windows path::string() e ANSI).
std::string ToUtf8(const std::filesystem::path& p);
std::filesystem::path FromUtf8(const std::string& s);

std::string HomeDir();
std::vector<Place> GetPlaces();  // Home, Desktop, Downloads, ...
std::vector<Place> GetDrives();  // "/", pendrives, C:\, D:\ ...

// Le um arquivo inteiro (aceita nomes UTF-8 tambem no Windows).
std::vector<unsigned char> ReadFileBytes(const std::string& utf8Path);
