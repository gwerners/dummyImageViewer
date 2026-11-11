#include "Core.h"
#include <fmt/color.h>
#include <fmt/format.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>
#include "Const.h"

#include "raylib.h"
#include "raymath.h"

#include "imgui.h"
#include "imgui_impl_raylib.h"
#include "rlImGui.h"

#include "Platform.h"
#include "Utils.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

struct Thumbnail {
  Texture2D texture;
  std::string filename;
  std::string filepath;
  Vector2 originalSize;
  bool loaded = false;  // textura ja enviada para a GPU
  bool failed = false;  // decodificacao falhou
  int id = 0;           // indice original (estavel entre ordenacoes)
};

// ---------------------------------------------------------------------------
// Estado global da UI
// ---------------------------------------------------------------------------
static std::vector<Thumbnail> thumbnails;
static int selectedImageIndex = -1;  // -1 = nenhuma selecionada
static std::string currentDirectory = ".";
static bool openImage = true;

static float thumbSize = 128.0f;
static bool showHidden = false;
static bool showFolders = true;
static bool sortDesc = false;
static bool editPath = false;
static char filterBuf[128] = "";
static char pathBuf[1024] = "";
static std::string browseStatus;

static fs::path browsePath;
static std::vector<fs::path> subDirs;
static std::vector<Place> places;
static std::vector<Place> drives;
static std::vector<fs::path> history;
static int histPos = -1;

// Pasta destacada na grade (ultima pasta em que entramos por duplo clique, ou
// a pasta de onde voltamos ao subir) e controle da arvore lateral.
static fs::path highlightDir;
static bool scrollToHighlight = false;
static float viewZoom = 1.0f;    // zoom do visualizador (toolbar + Ctrl+roda)
static bool treeReveal = false;  // true no frame seguinte a uma navegacao

// Modo apresentacao (slideshow em tela cheia)
static char presentBuf[3] = "5";     // segundos entre imagens (2 digitos)
static bool presenting = false;      // apresentacao em andamento
static bool presentMaxReq = false;   // pede ao visualizador p/ maximizar
static bool presentStopReq = false;  // pede ao visualizador p/ desfazer
static bool presentDidMax = false;   // fomos nos que maximizamos
static double presentTick = 0.0;     // instante da ultima troca
static int presentLastIdx = -1;      // detecta troca manual (mouse/setas)
static int presentShownIdx = -1;     // imagem ja carregada/exibida
static bool presentPaused = false;   // apresentacao pausada (Espaco)

static float PresentSeconds() {
  int v = atoi(presentBuf);
  return (float)std::clamp(v, 1, 99);
}

// Enquadramento global: 0 = original (full), 1 = altura visivel,
// 2 = largura visivel, 3 = altura e largura visiveis
static int viewFit = 0;

// Rotacao global (quartos de volta, sentido horario): vale para todas as
// imagens, no visualizador e na apresentacao, ate o usuario mudar.
static int viewRotation = 0;

// Troca manual de imagem: reinicia a contagem da apresentacao
static void ResetPresentTimer() {
  presentTick = GetTime();
}

static void StopPresentation() {
  if (!presenting)
    return;
  presenting = false;
  presentPaused = false;
  presentStopReq = true;
}

// Inicia a apresentacao na imagem atual (ou na primeira da pasta)
static void StartPresentation() {
  if (presenting || thumbnails.empty())
    return;
  if (selectedImageIndex < 0 || selectedImageIndex >= (int)thumbnails.size())
    selectedImageIndex = 0;
  openImage = true;
  presenting = true;
  presentPaused = false;
  presentMaxReq = true;
  presentTick = GetTime();
  presentLastIdx = selectedImageIndex;
  presentShownIdx = -1;  // forca reiniciar a contagem apos carregar
}

static bool showHelp = false;  // tela de atalhos (F1)
static std::map<std::string, std::vector<fs::path>> treeCache;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string Lower(std::string s) {
  for (auto& c : s)
    c = (char)std::tolower((unsigned char)c);
  return s;
}

static bool MatchFilter(const std::string& name) {
  if (!filterBuf[0])
    return true;
  return Lower(name).find(Lower(filterBuf)) != std::string::npos;
}

static bool IsHiddenName(const std::string& n) {
  return !n.empty() && n[0] == '.';
}

// Ordenacao "natural": img2 < img10, ignorando maiusculas
static bool NaturalLess(const std::string& a0, const std::string& b0) {
  std::string a = Lower(a0), b = Lower(b0);
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (std::isdigit((unsigned char)a[i]) &&
        std::isdigit((unsigned char)b[j])) {
      size_t si = i, sj = j;
      while (si < a.size() && a[si] == '0')
        ++si;
      while (sj < b.size() && b[sj] == '0')
        ++sj;
      size_t ei = si, ej = sj;
      while (ei < a.size() && std::isdigit((unsigned char)a[ei]))
        ++ei;
      while (ej < b.size() && std::isdigit((unsigned char)b[ej]))
        ++ej;
      if (ei - si != ej - sj)
        return (ei - si) < (ej - sj);
      int c = a.compare(si, ei - si, b, sj, ej - sj);
      if (c != 0)
        return c < 0;
      i = ei;
      j = ej;
    } else {
      if (a[i] != b[j])
        return a[i] < b[j];
      ++i;
      ++j;
    }
  }
  return (a.size() - i) < (b.size() - j);
}

// ---------------------------------------------------------------------------
// Carregamento de imagens
// ---------------------------------------------------------------------------
static bool IsSupportedImage(const std::string& lowerName) {
  static const char* exts[] = {".avif", ".webp", ".jxl", ".png",
                               ".jpg",  ".jpeg", ".bmp", ".tga",
                               ".ppm",  ".pgm",  ".qoi"};
  for (const char* e : exts)
    if (lowerName.ends_with(e))
      return true;
  return false;
}

Image LoadImageAnyFormat(const std::string& path) {
  std::string lower_path = Lower(path);
  if (lower_path.ends_with(".jxl")) {
    return LoadJXL(path.c_str());
  } else if (lower_path.ends_with(".avif")) {
    return LoadAVIF(path.c_str());
  } else if (lower_path.ends_with(".webp")) {
    return LoadWEBP(path.c_str());
  }
  static const char* native[] = {".png", ".jpg", ".jpeg", ".bmp",
                                 ".tga", ".ppm", ".pgm",  ".qoi"};
  for (const char* ext : native) {
    if (lower_path.ends_with(ext)) {
      // le os bytes nos mesmos (UTF-8 safe no Windows) e decodifica da memoria
      std::vector<unsigned char> bytes = ReadFileBytes(path);
      if (bytes.empty())
        return {0};
      return LoadImageFromMemory(ext, bytes.data(), (int)bytes.size());
    }
  }
  return {0};
}

// ---------------------------------------------------------------------------
// Carregamento em segundo plano: threads decodificam + reduzem a miniatura
// (so CPU); a thread principal envia as texturas para a GPU aos poucos.
// ---------------------------------------------------------------------------
struct LoadResult {
  int id;
  Image img;
  int w, h;
  bool ok;
};
struct LoadSession {
  std::vector<std::string> paths;
  std::atomic<bool> cancel{false};
  std::atomic<size_t> next{0};
  std::atomic<int> running{0};
  std::mutex mtx;
  std::vector<LoadResult> results;
  std::vector<std::thread> threads;
};
static std::shared_ptr<LoadSession> session;
static std::vector<std::shared_ptr<LoadSession>> graveyard;
static std::vector<size_t> idToPos;
static size_t loadedCount = 0, totalToLoad = 0;
constexpr int kThumbMax = 256;  // lado maior da miniatura em memoria

static void WorkerMain(std::shared_ptr<LoadSession> s) {
  for (;;) {
    if (s->cancel)
      break;
    size_t i = s->next.fetch_add(1);
    if (i >= s->paths.size())
      break;
    LoadResult r{(int)i, {0}, 0, 0, false};
    Image img = LoadImageAnyFormat(s->paths[i]);
    if (img.data) {
      r.w = img.width;
      r.h = img.height;
      int m = std::max(img.width, img.height);
      if (m > kThumbMax && !s->cancel) {
        float sc = (float)kThumbMax / (float)m;
        ImageResize(&img, std::max(1, (int)(img.width * sc)),
                    std::max(1, (int)(img.height * sc)));
      }
      r.img = img;
      r.ok = true;
    }
    std::lock_guard<std::mutex> lk(s->mtx);
    if (s->cancel) {  // sessao descartada: libera e sai
      if (r.ok)
        UnloadImage(r.img);
      break;
    }
    s->results.push_back(r);
  }
  s->running--;
}

static void CancelSession() {
  if (!session)
    return;
  {
    std::lock_guard<std::mutex> lk(session->mtx);
    session->cancel = true;
    for (auto& r : session->results)
      if (r.ok)
        UnloadImage(r.img);
    session->results.clear();
  }
  graveyard.push_back(session);
  session.reset();
  // junta e descarta as sessoes cujas threads ja terminaram
  for (size_t i = 0; i < graveyard.size();) {
    if (graveyard[i]->running == 0) {
      for (auto& t : graveyard[i]->threads)
        if (t.joinable())
          t.join();
      graveyard.erase(graveyard.begin() + (long)i);
    } else {
      ++i;
    }
  }
}

static void ShutdownLoader() {
  CancelSession();
  for (auto& g : graveyard)
    for (auto& t : g->threads)
      if (t.joinable())
        t.join();
  graveyard.clear();
}

static void RebuildIdToPos() {
  idToPos.assign(thumbnails.size(), 0);
  for (size_t p = 0; p < thumbnails.size(); ++p)
    if ((size_t)thumbnails[p].id < idToPos.size())
      idToPos[(size_t)thumbnails[p].id] = p;
}

// Envia ao maximo `kMaxPerFrame` miniaturas prontas para a GPU por frame.
static void PumpLoadResults() {
  if (!session)
    return;
  constexpr size_t kMaxPerFrame = 12;
  std::vector<LoadResult> batch;
  {
    std::lock_guard<std::mutex> lk(session->mtx);
    size_t n = std::min(kMaxPerFrame, session->results.size());
    batch.assign(session->results.begin(), session->results.begin() + (long)n);
    session->results.erase(session->results.begin(),
                           session->results.begin() + (long)n);
  }
  for (auto& r : batch) {
    if ((size_t)r.id >= idToPos.size()) {
      if (r.ok)
        UnloadImage(r.img);
      continue;
    }
    Thumbnail& t = thumbnails[idToPos[(size_t)r.id]];
    if (r.ok) {
      t.texture = LoadTextureFromImage(r.img);
      t.originalSize = {(float)r.w, (float)r.h};
      t.loaded = true;
      UnloadImage(r.img);
    } else {
      t.failed = true;
    }
    ++loadedCount;
  }
}

// Textura em resolucao total da imagem aberta no visualizador (sob demanda)
static Thumbnail viewThumb;
static bool viewValid = false;
static std::string viewPath, viewFailed;

static void ReleaseView() {
  if (viewValid)
    UnloadTexture(viewThumb.texture);
  viewValid = false;
  viewPath.clear();
  viewFailed.clear();
}

static const Thumbnail& EnsureView(const Thumbnail& t) {
  if (viewValid && viewPath == t.filepath)
    return viewThumb;
  if (viewFailed == t.filepath)
    return t;
  ReleaseView();
  Image img = LoadImageAnyFormat(t.filepath);
  if (!img.data) {
    viewFailed = t.filepath;
    return t;
  }
  viewThumb = {LoadTextureFromImage(img),
               t.filename,
               t.filepath,
               {(float)img.width, (float)img.height},
               true,
               false,
               t.id};
  UnloadImage(img);
  viewValid = true;
  viewPath = t.filepath;
  return viewThumb;
}

static void UnloadAllThumbnails() {
  for (auto& thumb : thumbnails)
    UnloadTexture(thumb.texture);
  thumbnails.clear();
  idToPos.clear();
}

static void SortThumbnails() {
  std::sort(thumbnails.begin(), thumbnails.end(),
            [](const Thumbnail& a, const Thumbnail& b) {
              return sortDesc ? NaturalLess(b.filename, a.filename)
                              : NaturalLess(a.filename, b.filename);
            });
  RebuildIdToPos();
  selectedImageIndex = -1;
}

static void SortSubDirs() {
  std::sort(subDirs.begin(), subDirs.end(),
            [](const fs::path& a, const fs::path& b) {
              std::string x = ToUtf8(a.filename()), y = ToUtf8(b.filename());
              return sortDesc ? NaturalLess(y, x) : NaturalLess(x, y);
            });
}

static void LoadThumbnails(const std::string& dirUtf8) {
  CancelSession();
  UnloadAllThumbnails();
  ReleaseView();
  selectedImageIndex = -1;
  currentDirectory = dirUtf8;
  loadedCount = totalToLoad = 0;

  // 1) lista rapida (so nomes) -> a grade aparece na hora com placeholders
  struct Entry {
    std::string name, path;
  };
  std::vector<Entry> entries;
  std::error_code dec;
  for (const auto& entry : fs::directory_iterator(
           FromUtf8(dirUtf8), fs::directory_options::skip_permission_denied,
           dec)) {
    std::error_code fec;
    if (!entry.is_regular_file(fec))
      continue;
    std::string filename = ToUtf8(entry.path().filename());
    if (!showHidden && IsHiddenName(filename))
      continue;
    if (!IsSupportedImage(Lower(filename)))
      continue;
    entries.push_back({filename, ToUtf8(entry.path())});
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    return sortDesc ? NaturalLess(b.name, a.name) : NaturalLess(a.name, b.name);
  });

  auto s = std::make_shared<LoadSession>();
  for (size_t i = 0; i < entries.size(); ++i) {
    s->paths.push_back(entries[i].path);
    thumbnails.push_back({Texture2D{},
                          entries[i].name,
                          entries[i].path,
                          {1, 1},
                          false,
                          false,
                          (int)i});
  }
  RebuildIdToPos();
  totalToLoad = entries.size();
  session = s;

  // 2) threads decodificam em segundo plano
  unsigned hw = std::thread::hardware_concurrency();
  int nThreads = (int)std::clamp<unsigned>(hw > 1 ? hw - 1 : 1, 2, 6);
  nThreads = (int)std::min<size_t>((size_t)nThreads,
                                   std::max<size_t>(1, entries.size()));
  s->running = nThreads;
  for (int i = 0; i < nThreads; ++i)
    s->threads.emplace_back(WorkerMain, s);
}

// ---------------------------------------------------------------------------
// Navegacao (historico, pasta atual) - pedidos sao adiados para o inicio do
// frame, assim nunca alteramos listas enquanto a UI itera sobre elas.
// ---------------------------------------------------------------------------
enum class NavKind { None, Path, History, Refresh };
static NavKind pendingKind = NavKind::None;
static fs::path pendingPath;
static int pendingDelta = 0;

static void RequestNav(const fs::path& p) {
  pendingKind = NavKind::Path;
  pendingPath = p;
}
static void RequestHistory(int delta) {
  pendingKind = NavKind::History;
  pendingDelta = delta;
}
static void RequestRefresh() {
  pendingKind = NavKind::Refresh;
}

static void RefreshSubDirs() {
  subDirs.clear();
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(
           browsePath, fs::directory_options::skip_permission_denied, ec)) {
    std::error_code ec2;
    if (!e.is_directory(ec2))
      continue;
    if (!showHidden && IsHiddenName(ToUtf8(e.path().filename())))
      continue;
    subDirs.push_back(e.path());
  }
  SortSubDirs();
}

static bool NavigateTo(const fs::path& p, bool pushHistory) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(p, ec);
  if (ec)
    c = p;
  if (!fs::is_directory(c, ec)) {
    browseStatus = "Pasta invalida ou inexistente: " + ToUtf8(p);
    return false;
  }
  browseStatus.clear();
  bool same = (c == browsePath);
  // ao subir/voltar para um ancestral, destaca a pasta de onde viemos
  if (!browsePath.empty() && !same) {
    for (fs::path q = browsePath; q.has_parent_path() && q.parent_path() != q;
         q = q.parent_path()) {
      if (q.parent_path() == c) {
        highlightDir = q;
        scrollToHighlight = true;
        break;
      }
    }
  }
  treeReveal = true;
  browsePath = c;
  snprintf(pathBuf, sizeof(pathBuf), "%s", ToUtf8(c).c_str());
  if (pushHistory && !same) {
    history.resize((size_t)(histPos + 1));
    history.push_back(c);
    histPos = (int)history.size() - 1;
  }
  RefreshSubDirs();
  LoadThumbnails(ToUtf8(c));
  return true;
}

static void ProcessPendingNav() {
  NavKind k = pendingKind;
  pendingKind = NavKind::None;
  if (k == NavKind::Path) {
    NavigateTo(pendingPath, true);
  } else if (k == NavKind::History) {
    int idx = histPos + pendingDelta;
    if (idx >= 0 && idx < (int)history.size()) {
      histPos = idx;
      NavigateTo(history[(size_t)idx], false);
    }
  } else if (k == NavKind::Refresh) {
    treeCache.clear();
    NavigateTo(browsePath, false);
  }
}

static bool CanGoUp() {
  return browsePath.has_parent_path() && browsePath.parent_path() != browsePath;
}

// ---------------------------------------------------------------------------
// Icones desenhados com primitivas (sem depender de fonte de icones, portavel)
// ---------------------------------------------------------------------------
enum class Icon {
  Back,
  Forward,
  Up,
  Refresh,
  Home,
  Folder,
  Drive,
  Eye,
  EyeOff,
  Edit,
  SortAsc,
  SortDesc,
  Pictures,
  Download,
  Desktop,
  Docs,
  Plus,
  Minus,
  Play,
  Stop,
  RotLeft,
  RotRight,
  FitOriginal,
  FitHeight,
  FitWidth,
  FitBoth
};

static Icon IconForKind(const std::string& k) {
  if (k == "home")
    return Icon::Home;
  if (k == "desktop")
    return Icon::Desktop;
  if (k == "download")
    return Icon::Download;
  if (k == "docs")
    return Icon::Docs;
  if (k == "pictures")
    return Icon::Pictures;
  if (k == "drive")
    return Icon::Drive;
  return Icon::Folder;
}

static void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 p0, float s, ImU32 col) {
  auto P = [&](float x, float y) { return ImVec2(p0.x + x * s, p0.y + y * s); };
  const float th = std::max(1.5f, s * 0.09f);
  auto Line = [&](float x1, float y1, float x2, float y2) {
    dl->AddLine(P(x1, y1), P(x2, y2), col, th);
  };
  switch (icon) {
    case Icon::Back:
      Line(0.82f, 0.5f, 0.2f, 0.5f);
      Line(0.45f, 0.25f, 0.2f, 0.5f);
      Line(0.45f, 0.75f, 0.2f, 0.5f);
      break;
    case Icon::Forward:
      Line(0.18f, 0.5f, 0.8f, 0.5f);
      Line(0.55f, 0.25f, 0.8f, 0.5f);
      Line(0.55f, 0.75f, 0.8f, 0.5f);
      break;
    case Icon::Up:
      Line(0.5f, 0.82f, 0.5f, 0.2f);
      Line(0.25f, 0.45f, 0.5f, 0.2f);
      Line(0.75f, 0.45f, 0.5f, 0.2f);
      break;
    case Icon::Refresh: {
      ImVec2 c = P(0.5f, 0.5f);
      float r = 0.3f * s, a0 = 0.6f;
      dl->PathArcTo(c, r, a0, 5.6f, 24);
      dl->PathStroke(col, 0, th);
      ImVec2 A(c.x + r * std::cos(a0), c.y + r * std::sin(a0));
      ImVec2 tip(A.x + 0.2f * s * std::sin(a0), A.y - 0.2f * s * std::cos(a0));
      ImVec2 b1(A.x + 0.15f * s * std::cos(a0), A.y + 0.15f * s * std::sin(a0));
      ImVec2 b2(A.x - 0.15f * s * std::cos(a0), A.y - 0.15f * s * std::sin(a0));
      dl->AddTriangleFilled(tip, b1, b2, col);
      break;
    }
    case Icon::Home:
      Line(0.12f, 0.5f, 0.5f, 0.16f);
      Line(0.5f, 0.16f, 0.88f, 0.5f);
      dl->AddRect(P(0.24f, 0.48f), P(0.76f, 0.84f), col, 0, 0, th);
      dl->AddRectFilled(P(0.43f, 0.62f), P(0.57f, 0.84f), col);
      break;
    case Icon::Folder:
      dl->AddRectFilled(P(0.08f, 0.2f), P(0.42f, 0.34f), col, s * 0.05f);
      dl->AddRectFilled(P(0.08f, 0.28f), P(0.92f, 0.8f), col, s * 0.06f);
      break;
    case Icon::Drive:
      dl->AddRect(P(0.1f, 0.34f), P(0.9f, 0.7f), col, s * 0.06f, 0, th);
      dl->AddCircleFilled(P(0.76f, 0.52f), s * 0.05f, col);
      Line(0.2f, 0.52f, 0.5f, 0.52f);
      break;
    case Icon::Eye:
    case Icon::EyeOff:
      dl->AddBezierQuadratic(P(0.08f, 0.5f), P(0.5f, 0.05f), P(0.92f, 0.5f),
                             col, th);
      dl->AddBezierQuadratic(P(0.08f, 0.5f), P(0.5f, 0.95f), P(0.92f, 0.5f),
                             col, th);
      dl->AddCircleFilled(P(0.5f, 0.5f), s * 0.11f, col);
      if (icon == Icon::EyeOff)
        Line(0.15f, 0.88f, 0.85f, 0.12f);
      break;
    case Icon::Edit:
      Line(0.2f, 0.8f, 0.72f, 0.28f);
      dl->AddTriangleFilled(P(0.14f, 0.86f), P(0.2f, 0.66f), P(0.34f, 0.8f),
                            col);
      Line(0.62f, 0.18f, 0.82f, 0.38f);
      break;
    case Icon::SortAsc:
    case Icon::SortDesc: {
      Line(0.1f, 0.28f, 0.4f, 0.28f);
      Line(0.1f, 0.5f, 0.55f, 0.5f);
      Line(0.1f, 0.72f, 0.7f, 0.72f);
      if (icon == Icon::SortAsc) {  // seta p/ cima
        Line(0.86f, 0.8f, 0.86f, 0.2f);
        Line(0.76f, 0.34f, 0.86f, 0.2f);
        Line(0.96f, 0.34f, 0.86f, 0.2f);
      } else {  // seta p/ baixo
        Line(0.86f, 0.2f, 0.86f, 0.8f);
        Line(0.76f, 0.66f, 0.86f, 0.8f);
        Line(0.96f, 0.66f, 0.86f, 0.8f);
      }
      break;
    }
    case Icon::Pictures:
      dl->AddRect(P(0.1f, 0.2f), P(0.9f, 0.8f), col, s * 0.05f, 0, th);
      dl->AddTriangleFilled(P(0.16f, 0.74f), P(0.4f, 0.4f), P(0.62f, 0.74f),
                            col);
      dl->AddTriangleFilled(P(0.5f, 0.74f), P(0.68f, 0.52f), P(0.84f, 0.74f),
                            col);
      dl->AddCircleFilled(P(0.72f, 0.34f), s * 0.07f, col);
      break;
    case Icon::Download:
      Line(0.5f, 0.15f, 0.5f, 0.65f);
      Line(0.28f, 0.45f, 0.5f, 0.67f);
      Line(0.72f, 0.45f, 0.5f, 0.67f);
      Line(0.2f, 0.84f, 0.8f, 0.84f);
      break;
    case Icon::Desktop:
      dl->AddRect(P(0.1f, 0.2f), P(0.9f, 0.64f), col, s * 0.05f, 0, th);
      Line(0.5f, 0.64f, 0.5f, 0.82f);
      Line(0.3f, 0.84f, 0.7f, 0.84f);
      break;
    case Icon::Docs:
      dl->AddRect(P(0.22f, 0.12f), P(0.78f, 0.88f), col, s * 0.05f, 0, th);
      Line(0.34f, 0.34f, 0.66f, 0.34f);
      Line(0.34f, 0.5f, 0.66f, 0.5f);
      Line(0.34f, 0.66f, 0.56f, 0.66f);
      break;
    case Icon::Plus:
      Line(0.2f, 0.5f, 0.8f, 0.5f);
      Line(0.5f, 0.2f, 0.5f, 0.8f);
      break;
    case Icon::Minus:
      Line(0.2f, 0.5f, 0.8f, 0.5f);
      break;
    case Icon::RotLeft:
    case Icon::RotRight: {
      // seta circular; RotLeft e o espelho horizontal de RotRight
      const float m = icon == Icon::RotLeft ? -1.0f : 1.0f;
      auto Q = [&](float x, float y) {  // x,y relativos ao centro (0.5,0.5)
        return P(0.5f + m * x, 0.5f + y);
      };
      const float r = 0.3f, a0 = -1.0f, a1 = 4.2f;
      for (int i = 0; i < 28; ++i) {
        float t0 = a0 + (a1 - a0) * i / 28.0f;
        float t1 = a0 + (a1 - a0) * (i + 1) / 28.0f;
        dl->AddLine(Q(r * std::cos(t0), r * std::sin(t0)),
                    Q(r * std::cos(t1), r * std::sin(t1)), col, th);
      }
      float ex = r * std::cos(a1), ey = r * std::sin(a1);
      float tx = -std::sin(a1), ty = std::cos(a1);  // tangente (horario)
      float nx = std::cos(a1), ny = std::sin(a1);
      dl->AddTriangleFilled(Q(ex + tx * 0.2f, ey + ty * 0.2f),
                            Q(ex + nx * 0.14f, ey + ny * 0.14f),
                            Q(ex - nx * 0.14f, ey - ny * 0.14f), col);
      break;
    }
    case Icon::FitOriginal:
    case Icon::FitHeight:
    case Icon::FitWidth:
    case Icon::FitBoth: {
      // quadrado central + setas apontando a direcao do ajuste
      const float a = 0.30f, b = 0.70f;
      dl->AddRect(P(a, a), P(b, b), col, 2.0f, 0, th);
      auto Arrow = [&](float x1, float y1, float x2, float y2) {
        Line(x1, y1, x2, y2);  // haste; cabeca na ponta (x2,y2)
        float dx = x2 - x1, dy = y2 - y1;
        float len = std::sqrt(dx * dx + dy * dy);
        dx /= len;
        dy /= len;
        const float h = 0.13f;  // comprimento da cabeca
        Line(x2, y2, x2 - dx * h - dy * h * 0.8f, y2 - dy * h + dx * h * 0.8f);
        Line(x2, y2, x2 - dx * h + dy * h * 0.8f, y2 - dy * h - dx * h * 0.8f);
      };
      if (icon == Icon::FitOriginal) {
        dl->AddRectFilled(P(0.42f, 0.42f), P(0.58f, 0.58f), col);
      } else if (icon == Icon::FitHeight || icon == Icon::FitBoth) {
        if (icon == Icon::FitHeight) {
          Arrow(0.5f, 0.26f, 0.5f, 0.04f);  // cima
          Arrow(0.5f, 0.74f, 0.5f, 0.96f);  // baixo
        }
      }
      if (icon == Icon::FitWidth) {
        Arrow(0.26f, 0.5f, 0.04f, 0.5f);  // esquerda
        Arrow(0.74f, 0.5f, 0.96f, 0.5f);  // direita
      }
      if (icon == Icon::FitBoth) {  // setas diagonais nos 4 cantos
        Arrow(0.27f, 0.27f, 0.05f, 0.05f);
        Arrow(0.73f, 0.27f, 0.95f, 0.05f);
        Arrow(0.27f, 0.73f, 0.05f, 0.95f);
        Arrow(0.73f, 0.73f, 0.95f, 0.95f);
      }
      break;
    }
    case Icon::Play:
      dl->AddTriangleFilled(P(0.28f, 0.18f), P(0.28f, 0.82f), P(0.82f, 0.5f),
                            col);
      break;
    case Icon::Stop:
      dl->AddRectFilled(P(0.22f, 0.22f), P(0.78f, 0.78f), col, 2.0f);
      break;
  }
}

// Botao quadrado com icone + hint (tooltip). `toggled` = estado ligado.
static bool IconButton(const char* id,
                       Icon icon,
                       const char* hint,
                       bool enabled = true,
                       bool toggled = false) {
  const float sz = 32.0f;
  ImGui::PushID(id);
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##b", ImVec2(sz, sz));
  bool hov = ImGui::IsItemHovered();
  bool act = ImGui::IsItemActive();
  bool clicked = ImGui::IsItemClicked(0);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (enabled && (hov || toggled)) {
    ImU32 bg = ImGui::GetColorU32(act   ? ImGuiCol_ButtonActive
                                  : hov ? ImGuiCol_ButtonHovered
                                        : ImGuiCol_Button);
    if (toggled && !hov)
      bg = ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.55f);
    dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), bg, 6.0f);
  }
  ImU32 col =
      ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
  DrawIcon(dl, icon, ImVec2(p.x + 6, p.y + 6), sz - 12, col);
  if (hov && hint && *hint)
    ImGui::SetTooltip("%s", hint);
  ImGui::PopID();
  return clicked && enabled;
}

// Posiciona o proximo item na mesma linha, centralizado em altura h
static void NextItem(float y0, float h) {
  ImGui::SameLine();
  ImGui::SetCursorPosY(y0 + (32.0f - h) * 0.5f);
}

// ---------------------------------------------------------------------------
// Atalhos de teclado
// ---------------------------------------------------------------------------
static void ProcessShortcuts() {
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput)
    return;
  if (openImage && selectedImageIndex >= 0)  // visualizador aberto
    return;
  if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
    RequestHistory(-1);
  if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow))
    RequestHistory(+1);
  if ((io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) ||
      ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
    if (CanGoUp())
      RequestNav(browsePath.parent_path());
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F5))
    RequestRefresh();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L))
    editPath = true;
}

// Atalhos globais do visualizador / apresentacao / ajuda
static void ProcessViewerShortcuts() {
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput)  // digitando no filtro / caminho / segundos
    return;
  auto Pressed = [](ImGuiKey k, bool repeat = false) {
    return ImGui::IsKeyPressed(k, repeat);
  };

  if (Pressed(ImGuiKey_F1))
    showHelp = !showHelp;
  if (io.KeyCtrl || io.KeyAlt)
    return;

  // P: inicia/para a apresentacao; Espaco: pausa/retoma
  if (Pressed(ImGuiKey_P)) {
    if (presenting)
      StopPresentation();
    else
      StartPresentation();
  }
  if (presenting && Pressed(ImGuiKey_Space)) {
    presentPaused = !presentPaused;
    presentTick = GetTime();
  }

  if (!(openImage && selectedImageIndex >= 0 && !thumbnails.empty()))
    return;

  // enquadramento
  static const ImGuiKey fitKeys[4] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3,
                                      ImGuiKey_4};
  static const ImGuiKey fitKeysPad[4] = {ImGuiKey_Keypad1, ImGuiKey_Keypad2,
                                         ImGuiKey_Keypad3, ImGuiKey_Keypad4};
  for (int i = 0; i < 4; ++i) {
    if (Pressed(fitKeys[i]) || Pressed(fitKeysPad[i])) {
      viewFit = i;
      viewZoom = 1.0f;
    }
  }
  // rotacao: R = direita, Shift+R = esquerda
  if (Pressed(ImGuiKey_R, true))
    viewRotation = ((viewRotation + (io.KeyShift ? -1 : 1)) % 4 + 4) % 4;
  // zoom
  if (Pressed(ImGuiKey_Equal, true) || Pressed(ImGuiKey_KeypadAdd, true))
    viewZoom = std::min(10.0f, viewZoom + 0.1f);
  if (Pressed(ImGuiKey_Minus, true) || Pressed(ImGuiKey_KeypadSubtract, true))
    viewZoom = std::max(0.1f, viewZoom - 0.1f);
  if (Pressed(ImGuiKey_0) || Pressed(ImGuiKey_Keypad0))
    viewZoom = 1.0f;
  // primeira / ultima imagem
  if (Pressed(ImGuiKey_Home)) {
    selectedImageIndex = 0;
    ResetPresentTimer();
  }
  if (Pressed(ImGuiKey_End)) {
    selectedImageIndex = (int)thumbnails.size() - 1;
    ResetPresentTimer();
  }
}

// Janela de ajuda com todos os atalhos (F1)
static void ShowHelpWindow(float W, float H) {
  if (!showHelp)
    return;
  struct Row {
    const char* keys;
    const char* what;
  };
  struct Section {
    const char* title;
    std::vector<Row> rows;
  };
  static const std::vector<Section> sections = {
      {"Navegacao de pastas",
       {{"Alt+Esquerda / Alt+Direita", "Voltar / avancar no historico"},
        {"Alt+Cima / Backspace", "Pasta acima"},
        {"F5", "Recarregar pasta"},
        {"Ctrl+L", "Digitar caminho"}}},
      {"Visualizador de imagem",
       {{"Setas Esquerda / Direita", "Imagem anterior / proxima"},
        {"Roda do mouse", "Imagem anterior / proxima"},
        {"Home / End", "Primeira / ultima imagem"},
        {"1 / 2 / 3 / 4",
         "Enquadramento: original / altura / largura / altura e largura"},
        {"R / Shift+R", "Girar para a direita / esquerda"},
        {"+ / - / 0", "Aumentar / diminuir / restaurar zoom"},
        {"Ctrl+Roda", "Zoom"},
        {"Botao esquerdo (arrastar)", "Mover a imagem"},
        {"Botao direito (clique)", "Maximizar / restaurar a janela"},
        {"Botao do meio (arrastar)", "Mover a janela"},
        {"Esc", "Fechar o visualizador"}}},
      {"Apresentacao",
       {{"P", "Iniciar / parar a apresentacao"},
        {"Espaco", "Pausar / retomar"},
        {"Esc", "Cancelar a apresentacao"}}},
      {"Geral", {{"F1", "Mostrar / ocultar esta ajuda"}}}};

  ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowFocus();  // fica acima do visualizador
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
  if (ImGui::Begin("Atalhos e ajuda (F1 ou Esc fecha)", &showHelp,
                   ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoCollapse)) {
    for (const auto& s : sections) {
      ImGui::SeparatorText(s.title);
      if (ImGui::BeginTable(s.title, 2, ImGuiTableFlags_SizingFixedFit)) {
        for (const auto& r : s.rows) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1), "%s", r.keys);
          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(r.what);
        }
        ImGui::EndTable();
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Barra de ferramentas (topo)
// ---------------------------------------------------------------------------
static void ShowBreadcrumbs(float width) {
  struct Crumb {
    std::string label;
    fs::path target;
  };
  std::vector<Crumb> crumbs;
  fs::path cur;
  for (const auto& part : browsePath) {
    if (part.empty())
      continue;
    bool isRootDir = (part == part.root_directory() && !part.has_root_name());
    if (isRootDir) {
      if (cur.empty()) {  // Linux: "/"
        cur = part;
        crumbs.push_back({"/", cur});
      } else {  // Windows: "C:" + "\"  -> aponta para "C:\"
        cur += part;
        if (!crumbs.empty())
          crumbs.back().target = cur;
      }
      continue;
    }
    cur = cur.empty() ? part : cur / part;
    crumbs.push_back({ToUtf8(part), cur});
  }

  // mostra so o final do caminho que cabe na largura disponivel
  const ImGuiStyle& st = ImGui::GetStyle();
  auto W = [&](const Crumb& c) {
    return ImGui::CalcTextSize(c.label.c_str()).x + st.FramePadding.x * 2 +
           st.ItemSpacing.x * 2 + 10.0f;
  };
  float used = 28.0f;
  size_t first = crumbs.size();
  while (first > 0 && used + W(crumbs[first - 1]) <= width) {
    used += W(crumbs[first - 1]);
    --first;
  }
  if (first == crumbs.size() && !crumbs.empty())
    first = crumbs.size() - 1;
  if (first > 0) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("...");
    ImGui::SameLine();
  }
  for (size_t i = first; i < crumbs.size(); ++i) {
    ImGui::PushID((int)i);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::SmallButton(crumbs[i].label.c_str()))
      RequestNav(crumbs[i].target);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", ToUtf8(crumbs[i].target).c_str());
    ImGui::PopID();
    if (i + 1 < crumbs.size()) {
      ImGui::SameLine();
      ImGui::TextDisabled(">");
      ImGui::SameLine();
    }
  }
}

static void ShowToolbar(float x, float y, float w, float h) {
  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(w, h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
  ImGui::Begin("##toolbar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();

  const float y0 = ImGui::GetCursorPosY();
  const bool canBack = histPos > 0;
  const bool canFwd = histPos + 1 < (int)history.size();

  if (IconButton("back", Icon::Back, "Voltar (Alt+Esquerda)", canBack))
    RequestHistory(-1);
  NextItem(y0, 32);
  if (IconButton("fwd", Icon::Forward, "Avancar (Alt+Direita)", canFwd))
    RequestHistory(+1);
  NextItem(y0, 32);
  if (IconButton("up", Icon::Up, "Pasta acima (Alt+Cima / Backspace)",
                 CanGoUp()))
    RequestNav(browsePath.parent_path());
  NextItem(y0, 32);
  if (IconButton("refresh", Icon::Refresh, "Recarregar pasta (F5)"))
    RequestRefresh();
  NextItem(y0, 32);
  if (IconButton("home", Icon::Home, "Ir para a pasta pessoal")) {
    std::string home = HomeDir();
    if (!home.empty())
      RequestNav(FromUtf8(home));
  }

  // largura reservada ao grupo da direita
  const float sp = ImGui::GetStyle().ItemSpacing.x;
  // Itens restantes: caminho, botao editar, filtro, slider e 3 botoes (4
  // botoes fixos de 32 + 7 espacos). Caminho/filtro/slider se adaptam a
  // largura disponivel para a toolbar nunca estourar a janela.
  const float fixedW = 32 * 4 + sp * 7 + 4.0f;
  // Apos um item o cursor volta ao inicio da linha, entao o espaco livre
  // ate a borda direita e medido a partir do fim do ultimo botao.
  const float rightEdge =
      ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
  const float restW = rightEdge - ImGui::GetItemRectMax().x - fixedW;
  float pathW = 140.0f, filterW = 170.0f, sliderW = 110.0f;
  if (restW >= pathW + filterW + sliderW) {
    pathW = restW - filterW - sliderW;  // sobra vai para o caminho
  } else {
    // pouco espaco: caminho fica com 40%, filtro/slider dividem o resto
    pathW = std::max(80.0f, restW * 0.4f);
    const float other = std::max(0.0f, restW - pathW);
    filterW = std::max(40.0f, other * 0.6f);
    sliderW = std::max(30.0f, other * 0.4f);
  }

  // caminho: breadcrumbs ou campo de texto
  NextItem(y0, 32);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 5));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
  ImGui::BeginChild(
      "##path", ImVec2(pathW, 32), ImGuiChildFlags_Borders,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  if (editPath) {
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive())
      ImGui::SetKeyboardFocusHere();
    bool enter = ImGui::InputText("##pathedit", pathBuf, sizeof(pathBuf),
                                  ImGuiInputTextFlags_EnterReturnsTrue |
                                      ImGuiInputTextFlags_AutoSelectAll);
    if (enter)
      RequestNav(FromUtf8(pathBuf));
    if (enter || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        (ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered()))
      editPath = false;
  } else {
    ShowBreadcrumbs(pathW - 20);
  }
  ImGui::EndChild();
  ImGui::PopStyleVar(2);

  NextItem(y0, 32);
  if (IconButton("edit", Icon::Edit, "Digitar caminho (Ctrl+L)", true,
                 editPath)) {
    editPath = !editPath;
    snprintf(pathBuf, sizeof(pathBuf), "%s", ToUtf8(browsePath).c_str());
  }

  NextItem(y0, ImGui::GetFrameHeight());
  ImGui::SetNextItemWidth(filterW);
  ImGui::InputTextWithHint("##filter", "Filtrar por nome...", filterBuf,
                           sizeof(filterBuf));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Mostra so pastas/imagens cujo nome contenha o texto");

  NextItem(y0, ImGui::GetFrameHeight());
  ImGui::SetNextItemWidth(sliderW);
  ImGui::SliderFloat("##size", &thumbSize, 64.0f, 256.0f, "%.0f px");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Tamanho das miniaturas");

  NextItem(y0, 32);
  if (IconButton("folders", Icon::Folder, "Mostrar/ocultar pastas na grade",
                 true, showFolders))
    showFolders = !showFolders;
  NextItem(y0, 32);
  if (IconButton("hidden", showHidden ? Icon::Eye : Icon::EyeOff,
                 "Mostrar arquivos/pastas ocultos", true, showHidden)) {
    showHidden = !showHidden;
    RequestRefresh();
  }
  NextItem(y0, 32);
  if (IconButton("sort", sortDesc ? Icon::SortDesc : Icon::SortAsc,
                 sortDesc ? "Ordem: Z-A (clique p/ A-Z)"
                          : "Ordem: A-Z (clique p/ Z-A)")) {
    sortDesc = !sortDesc;
    SortThumbnails();
    SortSubDirs();
  }

  ImGui::End();
}

// Segunda barra (abaixo da principal): rotacao da imagem aberta
static void ShowToolbar2(float x, float y, float w, float h) {
  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(w, h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
  ImGui::Begin("##toolbar2", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();

  const float y0 = ImGui::GetCursorPosY();
  const bool hasImage = openImage && selectedImageIndex >= 0 &&
                        selectedImageIndex < (int)thumbnails.size();
  int dir = 0;
  if (IconButton("rotleft", Icon::RotLeft, "Girar para a esquerda", hasImage))
    dir = -1;
  NextItem(y0, 32);
  if (IconButton("rotright", Icon::RotRight, "Girar para a direita", hasImage))
    dir = +1;
  if (dir != 0)
    viewRotation = ((viewRotation + dir) % 4 + 4) % 4;

  NextItem(y0, 32);
  if (IconButton("zoomout", Icon::Minus, "Diminuir zoom da imagem"))
    viewZoom = std::max(0.1f, viewZoom - 0.1f);
  NextItem(y0, 32);
  if (IconButton("zoomin", Icon::Plus, "Aumentar zoom da imagem"))
    viewZoom = std::min(10.0f, viewZoom + 0.1f);

  // Enquadramento da imagem (global: visualizador, navegacao e apresentacao)
  // (4 botoes em vez de combo: o popup do combo e fechado pelo OpenPopup
  // do visualizador, que roda a cada frame)
  {
    static const Icon fitIcons[4] = {Icon::FitOriginal, Icon::FitHeight,
                                     Icon::FitWidth, Icon::FitBoth};
    static const char* fitIds[4] = {"fit0", "fit1", "fit2", "fit3"};
    static const char* fitHints[4] = {
        "Enquadramento 1: tamanho original (tecla 1)",
        "Enquadramento 2: altura visivel (tecla 2)",
        "Enquadramento 3: largura visivel (tecla 3)",
        "Enquadramento 4: altura e largura visiveis (tecla 4)"};
    for (int i = 0; i < 4; ++i) {
      NextItem(y0, 32);
      if (IconButton(fitIds[i], fitIcons[i], fitHints[i], true, viewFit == i)) {
        viewFit = i;
        viewZoom = 1.0f;  // zoom antigo nao deve distorcer o novo ajuste
      }
    }
  }

  // Modo apresentacao: segundos entre imagens + play/stop
  NextItem(y0, ImGui::GetFrameHeight());
  ImGui::SetNextItemWidth(40.0f);
  ImGui::InputText("##presentsecs", presentBuf, sizeof(presentBuf),
                   ImGuiInputTextFlags_CharsDecimal);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Segundos entre imagens na apresentacao (1-99)");
  NextItem(y0, 32);
  if (IconButton("present", presenting ? Icon::Stop : Icon::Play,
                 presenting ? "Parar apresentacao (Esc)"
                            : "Apresentacao em tela cheia",
                 presenting || !thumbnails.empty(), presenting)) {
    if (presenting)
      StopPresentation();
    else
      StartPresentation();
  }

  // Ajuda (F1)
  NextItem(y0, 32);
  {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##help", ImVec2(32, 32));
    const bool hov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(0))
      showHelp = !showHelp;
    if (hov || showHelp)
      ImGui::GetWindowDrawList()->AddRectFilled(
          p, ImVec2(p.x + 32, p.y + 32),
          ImGui::GetColorU32(
              hov ? ImGuiCol_ButtonHovered : ImGuiCol_ButtonActive,
              hov ? 1.0f : 0.55f),
          6.0f);
    ImVec2 ts = ImGui::CalcTextSize("?");
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(p.x + (32 - ts.x) * 0.5f, p.y + (32 - ts.y) * 0.5f),
        ImGui::GetColorU32(ImGuiCol_Text), "?");
    if (hov)
      ImGui::SetTooltip("Atalhos e ajuda (F1)");
  }
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Barra lateral: locais + drives
// ---------------------------------------------------------------------------
static void SidebarItem(int id, const Place& pl) {
  const float h = 28.0f;
  fs::path target = FromUtf8(pl.path);
  bool selected = (browsePath == target);
  ImGui::PushID(id);
  ImVec2 p = ImGui::GetCursorScreenPos();
  if (ImGui::Selectable("##item", selected, 0, ImVec2(0, h)))
    RequestNav(target);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", pl.path.c_str());
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 tc = ImGui::GetColorU32(ImGuiCol_Text);
  ImU32 ic = pl.kind == "drive" || pl.kind == "home"
                 ? tc
                 : ImGui::GetColorU32(ImVec4(0.95f, 0.78f, 0.35f, 1));
  DrawIcon(dl, IconForKind(pl.kind), ImVec2(p.x + 6, p.y + 4), h - 8, ic);
  dl->AddText(ImVec2(p.x + 6 + h, p.y + (h - ImGui::GetFontSize()) * 0.5f), tc,
              pl.label.c_str());
  ImGui::PopID();
}

// Subpastas (em cache) de uma pasta da arvore
static const std::vector<fs::path>& TreeChildren(const fs::path& p) {
  std::string key = ToUtf8(p);
  auto it = treeCache.find(key);
  if (it != treeCache.end())
    return it->second;
  std::vector<fs::path> v;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(
           p, fs::directory_options::skip_permission_denied, ec)) {
    std::error_code ec2;
    if (!e.is_directory(ec2))
      continue;
    if (!showHidden && IsHiddenName(ToUtf8(e.path().filename())))
      continue;
    v.push_back(e.path());
  }
  std::sort(v.begin(), v.end(), [](const fs::path& a, const fs::path& b) {
    return NaturalLess(ToUtf8(a.filename()), ToUtf8(b.filename()));
  });
  return treeCache.emplace(key, std::move(v)).first->second;
}

// true se `p` e ancestral estrito da pasta atual
static bool IsAncestorOfBrowse(const fs::path& p) {
  for (fs::path q = browsePath; q.has_parent_path() && q.parent_path() != q;
       q = q.parent_path())
    if (q.parent_path() == p)
      return true;
  return false;
}

static void ShowTreeNode(const fs::path& p,
                         const std::string& label,
                         bool isRoot) {
  const auto& kids = TreeChildren(p);
  const bool selected = (p == browsePath);
  const bool ancestor = IsAncestorOfBrowse(p);
  ImGuiTreeNodeFlags fl =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (selected)
    fl |= ImGuiTreeNodeFlags_Selected;
  if (kids.empty())
    fl |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  if (isRoot)
    fl |= ImGuiTreeNodeFlags_DefaultOpen;
  if (treeReveal && ancestor)
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);

  ImGui::PushID(ToUtf8(p).c_str());
  bool open = ImGui::TreeNodeEx("##n", fl, "%s", label.c_str());
  if (ImGui::IsItemClicked(0) && !ImGui::IsItemToggledOpen())
    RequestNav(p);
  if (selected && treeReveal)
    ImGui::SetScrollHereY(0.5f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", ToUtf8(p).c_str());
  if (open && !kids.empty()) {
    for (const auto& k : kids)
      ShowTreeNode(k, ToUtf8(k.filename()), false);
    ImGui::TreePop();
  }
  ImGui::PopID();
}

static void ShowSidebar(float x, float y, float w, float h) {
  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(w, h));
  ImGui::Begin("##sidebar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar);
  ImGui::TextDisabled("LOCAIS");
  int id = 0;
  for (const auto& pl : places)
    SidebarItem(id++, pl);
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::TextDisabled("DRIVES");
  for (const auto& d : drives)
    SidebarItem(id++, d);
  ImGui::Spacing();
  if (ImGui::SmallButton("Atualizar drives")) {
    drives = GetDrives();
    places = GetPlaces();
  }
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::TextDisabled("ARVORE");
  fs::path root = browsePath.root_path();
  if (!root.empty()) {
    ImGui::BeginChild("##tree", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ShowTreeNode(root, ToUtf8(root), true);
    ImGui::EndChild();
  }
  treeReveal = false;
  ImGui::End();
}

static void ShowStatusBar(float x, float y, float w, float h) {
  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(w, h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
  ImGui::Begin("##status", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar();
  if (!browseStatus.empty())
    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", browseStatus.c_str());
  else {
    if (loadedCount < totalToLoad)
      ImGui::TextDisabled(
          "%zu pastas  |  %zu imagens (carregando %zu/%zu)  |  %s",
          subDirs.size(), thumbnails.size(), loadedCount, totalToLoad,
          currentDirectory.c_str());
    else
      ImGui::TextDisabled("%zu pastas  |  %zu imagens  |  %s", subDirs.size(),
                          thumbnails.size(), currentDirectory.c_str());
  }
  ImGui::End();
}

static void ShowThumbnailBrowser(float x, float y, float w, float h) {
  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(w, h));
  ImGui::Begin("Imagens", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImGui::BeginChild("ImageScrollArea", ImVec2(0, 0), false,
                    ImGuiWindowFlags_HorizontalScrollbar);

  float padding = 8.0f;
  float cellSize = thumbSize + padding;
  float panelWidth = ImGui::GetContentRegionAvail().x;
  int columns = (int)(panelWidth / cellSize);
  if (columns < 1)
    columns = 1;
  ImGui::Columns(columns, nullptr, false);

  // --- pastas (duplo clique entra) ---
  fs::path goTo;
  int shown = 0;
  if (showFolders) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < subDirs.size(); ++i) {
      std::string name = ToUtf8(subDirs[i].filename());
      if (!MatchFilter(name))
        continue;
      ++shown;
      ImGui::PushID(("dir:" + name).c_str());
      ImVec2 cur = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##folder", ImVec2(thumbSize, thumbSize));
      bool hov = ImGui::IsItemHovered();
      if (!highlightDir.empty() && subDirs[i] == highlightDir) {
        ImVec2 mx(cur.x + thumbSize, cur.y + thumbSize);
        dl->AddRectFilled(cur, mx, ImGui::GetColorU32(ImGuiCol_Header, 0.8f),
                          8.0f);
        dl->AddRect(cur, mx, ImGui::GetColorU32(ImGuiCol_HeaderActive), 8.0f, 0,
                    2.0f);
        if (scrollToHighlight) {
          ImGui::SetScrollHereY(0.5f);
          scrollToHighlight = false;
        }
      }
      if (hov) {
        dl->AddRectFilled(cur, ImVec2(cur.x + thumbSize, cur.y + thumbSize),
                          ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.5f),
                          8.0f);
        ImGui::SetTooltip("%s\n(duplo clique para abrir)", name.c_str());
        if (ImGui::IsMouseDoubleClicked(0)) {
          goTo = subDirs[i];
          highlightDir = subDirs[i];  // marca a pasta em que entramos
        }
      }
      float is = thumbSize * 0.62f;
      DrawIcon(dl, Icon::Folder,
               ImVec2(cur.x + (thumbSize - is) * 0.5f,
                      cur.y + (thumbSize - is) * 0.5f),
               is, ImGui::GetColorU32(ImVec4(0.95f, 0.78f, 0.35f, 1)));
      ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbSize);
      ImGui::TextWrapped("%s", name.c_str());
      ImGui::PopTextWrapPos();
      ImGui::Dummy(ImVec2(0.0f, 10.0f));
      ImGui::NextColumn();
      ImGui::PopID();
    }
  }

  // --- imagens ---
  for (size_t i = 0; i < thumbnails.size(); ++i) {
    const auto& thumb = thumbnails[i];
    if (!MatchFilter(thumb.filename))
      continue;
    ++shown;

    ImGui::PushID((int)i);
    // 1. Defina o tamanho da célula como de costume
    float cellSize = thumbSize;  // por exemplo: 128.0f ou valor dinâmico
    float imgW = thumb.originalSize.x;
    float imgH = thumb.originalSize.y;
    float aspect = imgW / imgH;

    // 2. Calcula tamanho proporcional dentro da célula
    float drawW, drawH;
    if (aspect > 1.0f) {
      drawW = cellSize;
      drawH = cellSize / aspect;
    } else {
      drawH = cellSize;
      drawW = cellSize * aspect;
    }

    // 3. Centraliza dentro da célula
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float offsetX = (cellSize - drawW) * 0.5f;
    float offsetY = (cellSize - drawH) * 0.5f;

    ImVec2 imagePos = ImVec2(cursor.x + offsetX, cursor.y + offsetY);
    ImGui::SetCursorScreenPos(imagePos);

    // 4. Desenha a imagem
    if (thumb.loaded) {
      ImGui::Image((ImTextureID)(intptr_t)thumb.texture.id,
                   ImVec2(drawW, drawH));
    } else {
      ImGui::Dummy(ImVec2(drawW, drawH));
      ImDrawList* pdl = ImGui::GetWindowDrawList();
      ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      pdl->AddRectFilled(a, b, IM_COL32(60, 60, 64, 255), 6.0f);
      const char* mark = thumb.failed ? "x" : "...";
      ImVec2 ts = ImGui::CalcTextSize(mark);
      pdl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f),
                   IM_COL32(150, 150, 150, 255), mark);
    }

    // 5. Volta cursor para baixo da célula
    ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + cellSize));

    // 🔥 Captura o click AGORA, antes de desenhar o nome!
    bool thumbnailClicked = ImGui::IsItemClicked();
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("%s\n%.0fx%.0f", thumb.filename.c_str(), thumb.originalSize.x,
                  thumb.originalSize.y);
      if (thumb.loaded)
        ImGui::Image((ImTextureID)(intptr_t)thumb.texture.id,
                     ImVec2(drawW * 2, drawH * 2));
      else
        ImGui::TextDisabled(thumb.failed ? "(falha ao decodificar)"
                                         : "(carregando...)");
      ImGui::EndTooltip();
    }
    {
      // Pega a posição da imagem
      float imgStartX = ImGui::GetItemRectMin().x;
      float imgEndY = ImGui::GetItemRectMax().y;

      // Move o cursor para logo abaixo da imagem
      ImGui::SetCursorScreenPos(ImVec2(imgStartX, imgEndY + 4.0f));

      // Centraliza o texto
      float textWidth = ImGui::CalcTextSize(thumb.filepath.c_str()).x;
      float textPosX = imgStartX + (thumbSize * 0.5f) - (textWidth * 0.5f);
      if (textPosX < imgStartX)
        textPosX = imgStartX;

      // Atualiza posição X
      ImGui::SetCursorScreenPos(
          ImVec2(textPosX, ImGui::GetCursorScreenPos().y));

      // Desenha o texto
      ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbSize);
      ImGui::TextWrapped("%s", thumb.filename.c_str());
      ImGui::PopTextWrapPos();

      // Dá um espaço para o próximo item
      ImGui::Dummy(ImVec2(0.0f, 10.0f));
    }

    // if (ImGui::IsItemClicked()) {
    if (thumbnailClicked) {
      selectedImageIndex = (int)i;
      openImage = true;
    }

    ImGui::NextColumn();
    ImGui::PopID();
  }

  ImGui::Columns(1);
  scrollToHighlight = false;
  if (shown == 0)
    ImGui::TextDisabled(filterBuf[0] ? "Nada corresponde ao filtro."
                                     : "Pasta vazia (sem imagens ou pastas).");
  ImGui::EndChild();
  ImGui::End();
  if (!goTo.empty())
    RequestNav(goTo);

  // Depois, antes de desenhar o popup (ou dentro dele), adicione isto:
  if (openImage && selectedImageIndex >= 0 && thumbnails.size() > 0 &&
      !ImGui::GetIO().WantTextInput) {  // nao navega enquanto digita
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_LEFT))
      ResetPresentTimer();
    if (IsKeyPressed(KEY_RIGHT)) {
      selectedImageIndex++;
      if (selectedImageIndex >= (int)thumbnails.size()) {
        selectedImageIndex = 0;  // Loop para o início
      }
    }
    if (IsKeyPressed(KEY_LEFT)) {
      if (selectedImageIndex == 0)
        selectedImageIndex = (int)thumbnails.size() - 1;  // Volta para a última
      else
        selectedImageIndex--;
    }
  }

  // Popup para mostrar imagem grande
  if (openImage && selectedImageIndex >= 0 &&
      selectedImageIndex < (int)thumbnails.size()) {
    static bool dragging = false;     // botao esquerdo: arrasta a imagem
    static bool winDragging = false;  // botao do meio: arrasta a janela
    static bool maximized = false;
    static bool restoreSaved = false;
    static bool leftDown = false;  // botao direito: maximiza/restaura
    static ImVec2 pressPos(0, 0), savedPos(0, 0), savedSize(800, 600);
    ImGuiIO& io = ImGui::GetIO();
    ImGui::OpenPopup("Imagem Grande");

    // Apresentacao: avanca a imagem quando o tempo acabar. Troca manual
    // (roda/setas) apenas reinicia a contagem.
    if (presenting) {
      const int n = (int)thumbnails.size();
      const double now = GetTime();
      // So avanca se a imagem atual ja foi carregada/exibida; a contagem
      // comeca depois do carregamento (ver presentShownIdx abaixo).
      if (presentPaused) {
        presentTick = now;  // pausado: a contagem recomeca ao retomar
      } else if (selectedImageIndex == presentShownIdx &&
                 now - presentTick >= PresentSeconds()) {
        selectedImageIndex = (selectedImageIndex + 1) % n;
        presentTick = now;  // (pasta de 1 imagem nao recarrega)
      }
      presentLastIdx = selectedImageIndex;
    }
    if (presentStopReq) {
      presentStopReq = false;
      if (presentDidMax && maximized) {
        maximized = false;
        restoreSaved = true;
      }
      presentDidMax = false;
    }
    ImGui::SetNextWindowSize(
        ImVec2(800, 600), ImGuiCond_FirstUseEver);  // tamanho inicial uma vez
    if (maximized) {
      ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
      ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    } else if (restoreSaved) {
      ImGui::SetNextWindowPos(savedPos, ImGuiCond_Always);
      ImGui::SetNextWindowSize(savedSize, ImGuiCond_Always);
      restoreSaved = false;
    }
    // titulo = nome + dimensoes; "###" mantem o ID da janela estavel
    const Thumbnail& titleSrc = EnsureView(thumbnails[selectedImageIndex]);
    // Imagem nova carregada (decodificacao e sincrona e pode demorar):
    // a contagem do slideshow so comeca agora.
    if (presentShownIdx != selectedImageIndex) {
      presentShownIdx = selectedImageIndex;
      presentTick = GetTime();
    }
    std::string viewTitle =
        fmt::format("{}  -  {} x {} px###ImagemGrande", titleSrc.filename,
                    (int)titleSrc.originalSize.x, (int)titleSrc.originalSize.y);
    // NoMove: o botao esquerdo (janela) nao arrasta; a janela e movida pelo
    // botao do meio
    ImGuiWindowFlags vf = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    if (maximized)
      vf |= ImGuiWindowFlags_NoResize;
    if (ImGui::Begin(viewTitle.c_str(), &openImage, vf)) {
      const ImVec2 winPos = ImGui::GetWindowPos();
      const ImVec2 winSize = ImGui::GetWindowSize();

      if (presentMaxReq) {
        presentMaxReq = false;
        if (!maximized) {
          savedPos = winPos;
          savedSize = winSize;
          maximized = true;
          presentDidMax = true;
        }
      }

      // Botao do meio: arrasta a janela (em qualquer ponto dela)
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && !maximized &&
          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        winDragging = true;
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        winDragging = false;
      if (winDragging)
        ImGui::SetWindowPos(
            ImVec2(winPos.x + io.MouseDelta.x, winPos.y + io.MouseDelta.y));

      ImGui::BeginChild("ImageScrollRegion", ImVec2(0, 0), true,
                        ImGuiWindowFlags_HorizontalScrollbar);

      const bool childHovered = ImGui::IsWindowHovered();

      // Botao esquerdo: arrasta a imagem (scroll)
      if (childHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        dragging = true;
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        dragging = false;
      if (dragging) {
        ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
      }

      // Botao direito (clique simples, sem arrastar): maximiza / restaura.
      // Ignora cliques sobre as barras de rolagem.
      if (childHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImVec2 cp = ImGui::GetWindowPos(), cs = ImGui::GetWindowSize();
        float sb = ImGui::GetStyle().ScrollbarSize;
        bool onBar =
            (ImGui::GetScrollMaxY() > 0 && io.MousePos.x > cp.x + cs.x - sb) ||
            (ImGui::GetScrollMaxX() > 0 && io.MousePos.y > cp.y + cs.y - sb);
        if (!onBar) {
          leftDown = true;
          pressPos = io.MousePos;
        }
      }
      if (leftDown && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        leftDown = false;
        float dx = io.MousePos.x - pressPos.x, dy = io.MousePos.y - pressPos.y;
        if (childHovered && dx * dx + dy * dy < 25.0f) {
          if (!maximized) {
            savedPos = winPos;
            savedSize = winSize;
            maximized = true;
          } else {
            maximized = false;
            restoreSaved = true;
          }
        }
      }
      // Roda do mouse: avanca/recua na lista (ordem alfabetica A-Z);
      // com Ctrl: zoom.
      float wheel = io.MouseWheel;
      if (wheel != 0.0f && childHovered) {
        if (io.KeyCtrl) {
          viewZoom = std::min(10.0f, std::max(0.1f, viewZoom + wheel * 0.1f));
        } else {
          // roda p/ baixo = proxima imagem (alfabeticamente), p/ cima =
          // anterior
          // mesma ordem da lista exibida (igual as setas e a apresentacao)
          int step = wheel < 0.0f ? 1 : -1;
          int n = (int)thumbnails.size();
          selectedImageIndex = ((selectedImageIndex + step) % n + n) % n;
          ResetPresentTimer();
          presentLastIdx = selectedImageIndex;
          ImGui::SetScrollX(0);
          ImGui::SetScrollY(0);
        }
      }
      const auto& thumb = EnsureView(thumbnails[selectedImageIndex]);

      // Zoom
      const float zoom = viewZoom;
      const ImGuiStyle& vst = ImGui::GetStyle();
      // area visivel estavel (nao depende de a barra de rolagem estar ativa)
      const ImVec2 inner = {ImGui::GetWindowSize().x - 2 * vst.WindowPadding.x -
                                2 * vst.ChildBorderSize - 1.0f,
                            ImGui::GetWindowSize().y - 2 * vst.WindowPadding.y -
                                2 * vst.ChildBorderSize - 1.0f};

      // Área de desenho
      const int rot = viewRotation;
      ImVec2 baseSize = {thumb.originalSize.x, thumb.originalSize.y};
      if (rot & 1)  // 90/270: largura e altura trocadas
        std::swap(baseSize.x, baseSize.y);

      // Enquadramento (global): 0 original, 1 altura, 2 largura, 3 ambos.
      // Nos modos de ajuste a imagem expande/encolhe ate caber; `zoom`
      // continua multiplicando.
      float scale = 1.0f;
      if (baseSize.x > 0 && baseSize.y > 0 && inner.x > 0 && inner.y > 0) {
        const float sb = vst.ScrollbarSize;
        if (viewFit == 1) {  // altura toda visivel
          scale = inner.y / baseSize.y;
          if (baseSize.x * scale > inner.x)  // barra horizontal aparece
            scale = (inner.y - sb) / baseSize.y;
        } else if (viewFit == 2) {  // largura toda visivel
          scale = inner.x / baseSize.x;
          if (baseSize.y * scale > inner.y)  // barra vertical aparece
            scale = (inner.x - sb) / baseSize.x;
        } else if (viewFit == 3) {  // altura e largura visiveis
          scale = std::min(inner.x / baseSize.x, inner.y / baseSize.y);
        }
      }
      ImVec2 imageSize = {baseSize.x * scale * zoom, baseSize.y * scale * zoom};

      // centraliza quando a imagem e menor que a area visivel
      ImGui::SetCursorPos(
          {vst.WindowPadding.x + std::max(0.0f, (inner.x - imageSize.x) * 0.5f),
           vst.WindowPadding.y +
               std::max(0.0f, (inner.y - imageSize.y) * 0.5f)});
      {
        // desenha o quad com UVs rotacionados (sentido horario)
        static const ImVec2 uvs[4][4] = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}},
                                         {{0, 1}, {0, 0}, {1, 0}, {1, 1}},
                                         {{1, 1}, {0, 1}, {0, 0}, {1, 0}},
                                         {{1, 0}, {1, 1}, {0, 1}, {0, 0}}};
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(imageSize);
        const ImVec2* uv = uvs[rot & 3];
        ImGui::GetWindowDrawList()->AddImageQuad(
            (ImTextureID)(intptr_t)thumb.texture.id, p,
            ImVec2(p.x + imageSize.x, p.y),
            ImVec2(p.x + imageSize.x, p.y + imageSize.y),
            ImVec2(p.x, p.y + imageSize.y), uv[0], uv[1], uv[2], uv[3]);
      }

      if (presenting && presentPaused) {
        const char* msg = "|| Pausado (Espaco retoma)";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        ImVec2 q(winPos.x + 16, winPos.y + 40);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddRectFilled(ImVec2(q.x - 8, q.y - 4),
                          ImVec2(q.x + ts.x + 8, q.y + ts.y + 4),
                          IM_COL32(0, 0, 0, 170), 6.0f);
        fg->AddText(q, IM_COL32(255, 255, 255, 255), msg);
      }

      ImGui::EndChild();
    }
    ImGui::End();  // sempre chamar End, mesmo se Begin retornar false

    if (!openImage) {
      selectedImageIndex = -1;
      presenting = false;  // fechou pelo X: encerra apresentacao
      presentDidMax = false;
      presentMaxReq = presentStopReq = false;
      // fechou pelo X: reseta o estado, senao reabre maximizada e sem resize
      maximized = false;
      restoreSaved = false;
      dragging = false;
      winDragging = false;
      leftDown = false;
    }
  }
}

bool Core::exists(const std::string& filename) {
  std::error_code ec;
  return fs::exists(FromUtf8(filename), ec);
}

std::string Core::readFile(const std::string& filename) {
  std::ifstream t(FromUtf8(filename));
  std::string ret = std::string((std::istreambuf_iterator<char>(t)),
                                std::istreambuf_iterator<char>());
  if (_priv._debug) {
    fmt::print(fg(fmt::color::blue), "{}:{} - {} contents[{}]\n", __FUNCTION__,
               __LINE__, filename, ret);
  }
  return ret;
}

Core::Core() {}

void Core::configure() {
  std::string filename(CONFIG_JSON_FILENAME);
  json data;
  try {
    data = json::parse(exists(filename) ? readFile(filename)
                                        : std::string(CONFIG_JSON));
  } catch (const std::exception& e) {
    fmt::print(fg(fmt::color::red),
               "{}:{} - config invalido ({}), usando padrao\n", __FUNCTION__,
               __LINE__, e.what());
    data = json::object();
  }
  _priv._debug = data.value("debug", false);
  _priv._root = data.value("root", std::string());
  if (_priv._root.empty() || !exists(_priv._root)) {
    if (!_priv._root.empty())
      fmt::print(fg(fmt::color::yellow),
                 "{}:{} - pasta configurada nao existe [{}], usando "
                 "fallback\n",
                 __FUNCTION__, __LINE__, _priv._root);
    std::string home = HomeDir();
    _priv._root = (!home.empty() && exists(home)) ? home : ".";
  }
}

void Core::run() {
  // force imediate flush
  setbuf(stdout, nullptr);
  fmt::print(fmt::emphasis::bold | fg(fmt::color::green), MSG_START);
  configure();

  int screenWidth = 1280;
  int screenHeight = 800;

  SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
  InitWindow(screenWidth, screenHeight, "Image Viewer");
  SetWindowMinSize(800, 500);
  SetExitKey(0);  // avoid ESC closing
  SetTargetFPS(144);
  rlImGuiSetup(true);
  {
    ImGuiStyle& st = ImGui::GetStyle();
    st.FrameRounding = 4.0f;
    st.GrabRounding = 4.0f;
    st.ScrollbarRounding = 6.0f;
    st.ItemSpacing = ImVec2(8, 6);
    st.WindowBorderHoverPadding =
        8.0f;  // facilita pegar a borda p/ redimensionar
  }

  places = GetPlaces();
  drives = GetDrives();
  NavigateTo(FromUtf8(_priv._root), true);

  bool running = true;
  while (running) {
    ProcessPendingNav();
    PumpLoadResults();
    ProcessShortcuts();
    ProcessViewerShortcuts();

    if (presenting && selectedImageIndex < 0)
      StopPresentation();  // imagem sumiu (ex.: navegou de pasta)
    if (IsKeyPressed(KEY_ESCAPE) && showHelp) {
      showHelp = false;  // Esc fecha primeiro a ajuda
    } else if (IsKeyPressed(KEY_ESCAPE) && presenting) {
      StopPresentation();  // Esc cancela so a apresentacao
    } else if (IsKeyPressed(KEY_ESCAPE) && !ImGui::GetIO().WantTextInput) {
      if (selectedImageIndex >= 0) {
        selectedImageIndex = -1;  // fecha o visualizador
      } else {
        running = false;
      }
    }
    BeginDrawing();
    ClearBackground(DARKGRAY);

    rlImGuiBegin();
    {
      const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
      const float toolbar1H = 48.0f, toolbar2H = 44.0f, statusH = 26.0f;
      const float toolbarH = toolbar1H + toolbar2H;
      const float sideW = std::min(300.0f, W * 0.3f);
      ShowToolbar(0, 0, W, toolbar1H);
      ShowToolbar2(0, toolbar1H, W, toolbar2H);
      ShowSidebar(0, toolbarH, sideW, H - toolbarH - statusH);
      ShowThumbnailBrowser(sideW, toolbarH, W - sideW, H - toolbarH - statusH);
      ShowStatusBar(0, H - statusH, W, statusH);
      ShowHelpWindow(W, H);
    }
    rlImGuiEnd();
    ImGui_ImplRaylib_RenderDrawData(ImGui::GetDrawData());
    EndDrawing();

    if (WindowShouldClose()) {
      running = false;
    }
  }
  ShutdownLoader();
  ReleaseView();
  UnloadAllThumbnails();
  rlImGuiShutdown();

  CloseWindow();
  return;
}
