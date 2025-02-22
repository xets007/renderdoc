/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2014-2019 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "common/dds_readwrite.h"
#include "core/core.h"
#include "replay/replay_driver.h"
#include "serialise/rdcfile.h"
#include "stb/stb_image.h"
#include "strings/string_utils.h"
#include "tinyexr/tinyexr.h"

class ImageViewer : public IReplayDriver
{
public:
  ImageViewer(IReplayDriver *proxy, const char *filename)
      : m_Proxy(proxy), m_Filename(filename), m_TextureID()
  {
    // start with props so that m_Props.localRenderer is correct
    m_Props = m_Proxy->GetAPIProperties();
    m_Props.pipelineType = GraphicsAPI::D3D11;
    m_Props.degraded = false;

    m_FrameRecord.frameInfo.fileOffset = 0;
    m_FrameRecord.frameInfo.frameNumber = 1;
    RDCEraseEl(m_FrameRecord.frameInfo.stats);

    m_FrameRecord.drawcallList.resize(1);
    DrawcallDescription &d = m_FrameRecord.drawcallList[0];
    d.drawcallId = 1;
    d.eventId = 1;
    d.name = filename;

    RefreshFile();

    m_Resources.push_back(ResourceDescription());
    m_Resources[0].resourceId = m_TextureID;
    m_Resources[0].autogeneratedName = false;
    m_Resources[0].name = get_basename(m_Filename);

    m_PipelineState.outputMerger.renderTargets.resize(1);
    m_PipelineState.outputMerger.renderTargets[0].resourceResourceId = m_TextureID;
  }

  virtual ~ImageViewer()
  {
    m_Proxy->Shutdown();
    m_Proxy = NULL;
  }

  bool IsRemoteProxy() { return true; }
  void Shutdown() { delete this; }
  // pass through necessary operations to proxy
  std::vector<WindowingSystem> GetSupportedWindowSystems()
  {
    return m_Proxy->GetSupportedWindowSystems();
  }
  AMDRGPControl *GetRGPControl() { return NULL; }
  uint64_t MakeOutputWindow(WindowingData window, bool depth)
  {
    return m_Proxy->MakeOutputWindow(window, depth);
  }
  void DestroyOutputWindow(uint64_t id) { m_Proxy->DestroyOutputWindow(id); }
  bool CheckResizeOutputWindow(uint64_t id) { return m_Proxy->CheckResizeOutputWindow(id); }
  void SetOutputWindowDimensions(uint64_t id, int32_t w, int32_t h)
  {
    m_Proxy->SetOutputWindowDimensions(id, w, h);
  }
  void GetOutputWindowDimensions(uint64_t id, int32_t &w, int32_t &h)
  {
    m_Proxy->GetOutputWindowDimensions(id, w, h);
  }
  void GetOutputWindowData(uint64_t id, bytebuf &retData)
  {
    m_Proxy->GetOutputWindowData(id, retData);
  }
  void ClearOutputWindowColor(uint64_t id, FloatVector col)
  {
    m_Proxy->ClearOutputWindowColor(id, col);
  }
  void ClearOutputWindowDepth(uint64_t id, float depth, uint8_t stencil)
  {
    m_Proxy->ClearOutputWindowDepth(id, depth, stencil);
  }
  void BindOutputWindow(uint64_t id, bool depth) { m_Proxy->BindOutputWindow(id, depth); }
  bool IsOutputWindowVisible(uint64_t id) { return m_Proxy->IsOutputWindowVisible(id); }
  void FlipOutputWindow(uint64_t id) { m_Proxy->FlipOutputWindow(id); }
  void RenderCheckerboard() { m_Proxy->RenderCheckerboard(); }
  void RenderHighlightBox(float w, float h, float scale)
  {
    m_Proxy->RenderHighlightBox(w, h, scale);
  }
  bool GetMinMax(ResourceId texid, uint32_t sliceFace, uint32_t mip, uint32_t sample,
                 CompType typeHint, float *minval, float *maxval)
  {
    return m_Proxy->GetMinMax(m_TextureID, sliceFace, mip, sample, typeHint, minval, maxval);
  }
  bool GetHistogram(ResourceId texid, uint32_t sliceFace, uint32_t mip, uint32_t sample,
                    CompType typeHint, float minval, float maxval, bool channels[4],
                    std::vector<uint32_t> &histogram)
  {
    return m_Proxy->GetHistogram(m_TextureID, sliceFace, mip, sample, typeHint, minval, maxval,
                                 channels, histogram);
  }
  bool RenderTexture(TextureDisplay cfg)
  {
    if(cfg.resourceId != m_TextureID && cfg.resourceId != m_CustomTexID)
      cfg.resourceId = m_TextureID;
    return m_Proxy->RenderTexture(cfg);
  }
  void PickPixel(ResourceId texture, uint32_t x, uint32_t y, uint32_t sliceFace, uint32_t mip,
                 uint32_t sample, CompType typeHint, float pixel[4])
  {
    m_Proxy->PickPixel(m_TextureID, x, y, sliceFace, mip, sample, typeHint, pixel);
  }
  uint32_t PickVertex(uint32_t eventId, int32_t width, int32_t height, const MeshDisplay &cfg,
                      uint32_t x, uint32_t y)
  {
    return m_Proxy->PickVertex(eventId, width, height, cfg, x, y);
  }
  rdcarray<ShaderEncoding> GetTargetShaderEncodings()
  {
    return m_Proxy->GetTargetShaderEncodings();
  }
  rdcarray<ShaderEncoding> GetCustomShaderEncodings()
  {
    return m_Proxy->GetCustomShaderEncodings();
  }
  void BuildCustomShader(ShaderEncoding sourceEncoding, bytebuf source, const std::string &entry,
                         const ShaderCompileFlags &compileFlags, ShaderStage type, ResourceId *id,
                         std::string *errors)
  {
    m_Proxy->BuildCustomShader(sourceEncoding, source, entry, compileFlags, type, id, errors);
  }
  void FreeCustomShader(ResourceId id) { m_Proxy->FreeTargetResource(id); }
  ResourceId ApplyCustomShader(ResourceId shader, ResourceId texid, uint32_t mip, uint32_t arrayIdx,
                               uint32_t sampleIdx, CompType typeHint)
  {
    m_CustomTexID =
        m_Proxy->ApplyCustomShader(shader, m_TextureID, mip, arrayIdx, sampleIdx, typeHint);
    return m_CustomTexID;
  }
  const std::vector<ResourceDescription> &GetResources() { return m_Resources; }
  std::vector<ResourceId> GetTextures() { return {m_TextureID}; }
  TextureDescription GetTexture(ResourceId id) { return m_Proxy->GetTexture(m_TextureID); }
  void GetTextureData(ResourceId tex, uint32_t arrayIdx, uint32_t mip,
                      const GetTextureDataParams &params, bytebuf &data)
  {
    m_Proxy->GetTextureData(m_TextureID, arrayIdx, mip, params, data);
  }

  // handle a couple of operations ourselves to return a simple fake log
  APIProperties GetAPIProperties() { return m_Props; }
  FrameRecord GetFrameRecord() { return m_FrameRecord; }
  const D3D11Pipe::State *GetD3D11PipelineState() { return &m_PipelineState; }
  // other operations are dropped/ignored, to avoid confusion
  ReplayStatus ReadLogInitialisation(RDCFile *rdc, bool storeStructuredBuffers)
  {
    return ReplayStatus::Succeeded;
  }
  const SDFile &GetStructuredFile() { return m_File; }
  void RenderMesh(uint32_t eventId, const std::vector<MeshFormat> &secondaryDraws,
                  const MeshDisplay &cfg)
  {
  }
  std::vector<ResourceId> GetBuffers() { return std::vector<ResourceId>(); }
  std::vector<DebugMessage> GetDebugMessages() { return std::vector<DebugMessage>(); }
  BufferDescription GetBuffer(ResourceId id)
  {
    BufferDescription ret;
    RDCEraseEl(ret);
    return ret;
  }
  void SavePipelineState(uint32_t eventId) {}
  DriverInformation GetDriverInfo()
  {
    DriverInformation ret = {};
    return ret;
  }
  rdcarray<GPUDevice> GetAvailableGPUs() { return {}; }
  const D3D12Pipe::State *GetD3D12PipelineState() { return NULL; }
  const GLPipe::State *GetGLPipelineState() { return NULL; }
  const VKPipe::State *GetVulkanPipelineState() { return NULL; }
  void ReplayLog(uint32_t endEventID, ReplayLogType replayType) {}
  std::vector<uint32_t> GetPassEvents(uint32_t eventId) { return std::vector<uint32_t>(); }
  std::vector<EventUsage> GetUsage(ResourceId id) { return std::vector<EventUsage>(); }
  bool IsRenderOutput(ResourceId id) { return false; }
  ResourceId GetLiveID(ResourceId id) { return id; }
  std::vector<GPUCounter> EnumerateCounters() { return std::vector<GPUCounter>(); }
  CounterDescription DescribeCounter(GPUCounter counterID)
  {
    CounterDescription desc = {};
    desc.counter = counterID;
    return desc;
  }
  std::vector<CounterResult> FetchCounters(const std::vector<GPUCounter> &counters)
  {
    return std::vector<CounterResult>();
  }
  void FillCBufferVariables(ResourceId pipeline, ResourceId shader, std::string entryPoint,
                            uint32_t cbufSlot, rdcarray<ShaderVariable> &outvars, const bytebuf &data)
  {
  }
  void GetBufferData(ResourceId buff, uint64_t offset, uint64_t len, bytebuf &retData) {}
  void InitPostVSBuffers(uint32_t eventId) {}
  void InitPostVSBuffers(const std::vector<uint32_t> &eventId) {}
  MeshFormat GetPostVSBuffers(uint32_t eventId, uint32_t instID, uint32_t viewID, MeshDataStage stage)
  {
    MeshFormat ret;
    RDCEraseEl(ret);
    return ret;
  }
  ResourceId RenderOverlay(ResourceId texid, CompType typeHint, FloatVector clearCol,
                           DebugOverlay overlay, uint32_t eventId,
                           const std::vector<uint32_t> &passEvents)
  {
    return ResourceId();
  }
  rdcarray<ShaderEntryPoint> GetShaderEntryPoints(ResourceId shader) { return {}; }
  ShaderReflection *GetShader(ResourceId pipeline, ResourceId shader, ShaderEntryPoint entry)
  {
    return NULL;
  }
  std::vector<std::string> GetDisassemblyTargets() { return {"N/A"}; }
  std::string DisassembleShader(ResourceId pipeline, const ShaderReflection *refl,
                                const std::string &target)
  {
    return "";
  }
  void FreeTargetResource(ResourceId id) {}
  std::vector<PixelModification> PixelHistory(std::vector<EventUsage> events, ResourceId target,
                                              uint32_t x, uint32_t y, uint32_t slice, uint32_t mip,
                                              uint32_t sampleIdx, CompType typeHint)
  {
    return std::vector<PixelModification>();
  }
  ShaderDebugTrace DebugVertex(uint32_t eventId, uint32_t vertid, uint32_t instid, uint32_t idx,
                               uint32_t instOffset, uint32_t vertOffset)
  {
    ShaderDebugTrace ret;
    RDCEraseEl(ret);
    return ret;
  }
  ShaderDebugTrace DebugPixel(uint32_t eventId, uint32_t x, uint32_t y, uint32_t sample,
                              uint32_t primitive)
  {
    ShaderDebugTrace ret;
    RDCEraseEl(ret);
    return ret;
  }
  ShaderDebugTrace DebugThread(uint32_t eventId, const uint32_t groupid[3],
                               const uint32_t threadid[3])
  {
    ShaderDebugTrace ret;
    RDCEraseEl(ret);
    return ret;
  }
  void BuildTargetShader(ShaderEncoding sourceEncoding, bytebuf source, const std::string &entry,
                         const ShaderCompileFlags &compileFlags, ShaderStage type, ResourceId *id,
                         std::string *errors)
  {
    if(id)
      *id = ResourceId();
    if(errors)
      *errors = "Building target shaders is unsupported";
  }
  void ReplaceResource(ResourceId from, ResourceId to) {}
  void RemoveReplacement(ResourceId id) {}
  // these are proxy functions, and will never be used
  ResourceId CreateProxyTexture(const TextureDescription &templateTex)
  {
    RDCERR("Calling proxy-render functions on an image viewer");
    return ResourceId();
  }

  void SetProxyTextureData(ResourceId texid, uint32_t arrayIdx, uint32_t mip, byte *data,
                           size_t dataSize)
  {
    RDCERR("Calling proxy-render functions on an image viewer");
  }
  bool IsTextureSupported(const ResourceFormat &format) { return true; }
  bool NeedRemapForFetch(const ResourceFormat &format) { return false; }
  ResourceId CreateProxyBuffer(const BufferDescription &templateBuf)
  {
    RDCERR("Calling proxy-render functions on an image viewer");
    return ResourceId();
  }

  void SetProxyBufferData(ResourceId bufid, byte *data, size_t dataSize)
  {
    RDCERR("Calling proxy-render functions on an image viewer");
  }

  void FileChanged() { RefreshFile(); }
private:
  void RefreshFile();

  APIProperties m_Props;
  FrameRecord m_FrameRecord;
  D3D11Pipe::State m_PipelineState;
  IReplayDriver *m_Proxy;
  std::string m_Filename;
  ResourceId m_TextureID, m_CustomTexID;
  std::vector<ResourceDescription> m_Resources;
  SDFile m_File;
  TextureDescription m_TexDetails;
};

ReplayStatus IMG_CreateReplayDevice(RDCFile *rdc, IReplayDriver **driver)
{
  if(!rdc)
    return ReplayStatus::InternalError;

  std::string filename;
  FILE *f = rdc->StealImageFileHandle(filename);

  if(!f)
    return ReplayStatus::FileIOFailed;

  // make sure the file is a type we recognise before going further
  if(is_exr_file(f))
  {
    const char *err = NULL;

    FileIO::fseek64(f, 0, SEEK_END);
    uint64_t size = FileIO::ftell64(f);
    FileIO::fseek64(f, 0, SEEK_SET);

    std::vector<byte> buffer;
    buffer.resize((size_t)size);

    FileIO::fread(&buffer[0], 1, buffer.size(), f);

    EXRVersion exrVersion;
    int ret = ParseEXRVersionFromMemory(&exrVersion, buffer.data(), buffer.size());

    if(ret != 0)
    {
      RDCERR("EXR file detected, but couldn't load with ParseEXRVersionFromMemory: %d", ret);
      FileIO::fclose(f);
      return ReplayStatus::ImageUnsupported;
    }

    if(exrVersion.multipart || exrVersion.non_image || exrVersion.tiled)
    {
      RDCERR("Unsupported EXR file detected - multipart or similar.");
      FileIO::fclose(f);
      return ReplayStatus::ImageUnsupported;
    }

    EXRHeader exrHeader;
    InitEXRHeader(&exrHeader);

    ret = ParseEXRHeaderFromMemory(&exrHeader, &exrVersion, buffer.data(), buffer.size(), &err);

    if(ret != 0)
    {
      RDCERR("EXR file detected, but couldn't load with ParseEXRHeaderFromMemory %d: '%s'", ret, err);
      FileIO::fclose(f);
      return ReplayStatus::ImageUnsupported;
    }
  }
  else if(stbi_is_hdr_from_file(f))
  {
    FileIO::fseek64(f, 0, SEEK_SET);

    int ignore = 0;
    float *data = stbi_loadf_from_file(f, &ignore, &ignore, &ignore, 4);

    if(!data)
    {
      FileIO::fclose(f);
      RDCERR("HDR file recognised, but couldn't load with stbi_loadf_from_file");
      return ReplayStatus::ImageUnsupported;
    }

    free(data);
  }
  else if(is_dds_file(f))
  {
    FileIO::fseek64(f, 0, SEEK_SET);
    dds_data read_data = load_dds_from_file(f);

    if(read_data.subdata == NULL)
    {
      FileIO::fclose(f);
      RDCERR("DDS file recognised, but couldn't load");
      return ReplayStatus::ImageUnsupported;
    }

    for(int i = 0; i < read_data.slices * read_data.mips; i++)
      delete[] read_data.subdata[i];

    delete[] read_data.subdata;
    delete[] read_data.subsizes;
  }
  else
  {
    int width = 0, height = 0;
    int ignore = 0;
    int ret = stbi_info_from_file(f, &width, &height, &ignore);

    // just in case (we shouldn't have come in here if this weren't true), make sure
    // the format is supported
    if(ret == 0 || width <= 0 || width >= 65536 || height <= 0 || height >= 65536)
    {
      FileIO::fclose(f);
      return ReplayStatus::ImageUnsupported;
    }

    byte *data = stbi_load_from_file(f, &ignore, &ignore, &ignore, 4);

    if(!data)
    {
      FileIO::fclose(f);
      RDCERR("File recognised, but couldn't load with stbi_load_from_file");
      return ReplayStatus::ImageUnsupported;
    }

    free(data);
  }

  FileIO::fclose(f);

  IReplayDriver *proxy = NULL;
  ReplayStatus status = RenderDoc::Inst().CreateProxyReplayDriver(RDCDriver::Unknown, &proxy);

  if(status != ReplayStatus::Succeeded || !proxy)
  {
    RDCERR("Couldn't create replay driver to proxy-render images");

    if(proxy)
      proxy->Shutdown();
    return status;
  }

  *driver = new ImageViewer(proxy, filename.c_str());

  if((*driver)->GetResources()[0].resourceId == ResourceId())
  {
    (*driver)->Shutdown();
    return ReplayStatus::ImageUnsupported;
  }

  return ReplayStatus::Succeeded;
}

void ImageViewer::RefreshFile()
{
  FILE *f = NULL;

  for(int attempt = 0; attempt < 10 && f == NULL; attempt++)
  {
    f = FileIO::fopen(m_Filename.c_str(), "rb");
    if(f)
      break;
    Threading::Sleep(40);
  }

  if(!f)
  {
    RDCERR("Couldn't open %s! Exclusive lock elsewhere?", m_Filename.c_str());
    return;
  }

  TextureDescription texDetails;

  ResourceFormat rgba8_unorm;
  rgba8_unorm.compByteWidth = 1;
  rgba8_unorm.compCount = 4;
  rgba8_unorm.compType = CompType::UNormSRGB;
  rgba8_unorm.type = ResourceFormatType::Regular;

  ResourceFormat rgba32_float = rgba8_unorm;
  rgba32_float.compByteWidth = 4;
  rgba32_float.compType = CompType::Float;

  texDetails.creationFlags = TextureCategory::SwapBuffer | TextureCategory::ColorTarget;
  texDetails.cubemap = false;
  texDetails.resourceId = m_TextureID;
  texDetails.byteSize = 0;
  texDetails.msQual = 0;
  texDetails.msSamp = 1;
  texDetails.format = rgba8_unorm;

  // reasonable defaults
  texDetails.type = TextureType::Texture2D;
  texDetails.dimension = 2;
  texDetails.arraysize = 1;
  texDetails.width = 1;
  texDetails.height = 1;
  texDetails.depth = 1;
  texDetails.mips = 1;

  byte *data = NULL;
  size_t datasize = 0;

  bool dds = false;

  if(is_exr_file(f))
  {
    texDetails.format = rgba32_float;

    FileIO::fseek64(f, 0, SEEK_END);
    uint64_t size = FileIO::ftell64(f);
    FileIO::fseek64(f, 0, SEEK_SET);

    std::vector<byte> buffer;
    buffer.resize((size_t)size);

    FileIO::fread(buffer.data(), 1, buffer.size(), f);

    EXRVersion exrVersion;
    int ret = ParseEXRVersionFromMemory(&exrVersion, buffer.data(), buffer.size());

    if(ret != 0)
    {
      RDCERR("EXR file detected, but couldn't load with ParseEXRVersionFromMemory: %d", ret);
      FileIO::fclose(f);
      return;
    }

    if(exrVersion.multipart || exrVersion.non_image || exrVersion.tiled)
    {
      RDCERR("Unsupported EXR file detected - multipart or similar.");
      FileIO::fclose(f);
      return;
    }

    EXRHeader exrHeader;
    InitEXRHeader(&exrHeader);

    const char *err = NULL;

    ret = ParseEXRHeaderFromMemory(&exrHeader, &exrVersion, buffer.data(), buffer.size(), &err);
    if(ret != 0)
    {
      RDCERR("EXR file detected, but couldn't load with ParseEXRHeaderFromMemory %d: '%s'", ret, err);
      FileIO::fclose(f);
      return;
    }

    for(int i = 0; i < exrHeader.num_channels; i++)
      exrHeader.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;

    EXRImage exrImage;
    InitEXRImage(&exrImage);

    ret = LoadEXRImageFromMemory(&exrImage, &exrHeader, buffer.data(), buffer.size(), &err);
    if(ret != 0)
    {
      RDCERR("EXR file detected, but couldn't load with LoadEXRImageFromMemory %d: '%s'", ret, err);
      FileIO::fclose(f);
      return;
    }

    texDetails.width = exrImage.width;
    texDetails.height = exrImage.height;

    datasize = texDetails.width * texDetails.height * 4 * sizeof(float);
    data = (byte *)malloc(datasize);

    int channels[4] = {-1, -1, -1, -1};
    for(int i = 0; i < exrImage.num_channels; i++)
    {
      switch(exrHeader.channels[i].name[0])
      {
        case 'R': channels[0] = i; break;
        case 'G': channels[1] = i; break;
        case 'B': channels[2] = i; break;
        case 'A': channels[3] = i; break;
      }
    }

    float *rgba = (float *)data;
    float **src = (float **)exrImage.images;

    for(uint32_t i = 0; i < texDetails.width * texDetails.height; i++)
    {
      for(int c = 0; c < 4; c++)
      {
        if(channels[c] >= 0)
          rgba[i * 4 + c] = src[channels[c]][i];
        else if(c < 3)    // RGB channels default to 0
          rgba[i * 4 + c] = 0.0f;
        else    // alpha defaults to 1
          rgba[i * 4 + c] = 1.0f;
      }
    }

    FreeEXRImage(&exrImage);

    // shouldn't get here but let's be safe
    if(ret != 0)
    {
      free(data);
      RDCERR("EXR file detected, but couldn't load with LoadEXRFromMemory %d: '%s'", ret, err);
      FileIO::fclose(f);
      return;
    }
  }
  else if(stbi_is_hdr_from_file(f))
  {
    texDetails.format = rgba32_float;

    FileIO::fseek64(f, 0, SEEK_SET);

    int ignore = 0;
    data = (byte *)stbi_loadf_from_file(f, (int *)&texDetails.width, (int *)&texDetails.height,
                                        &ignore, 4);
    datasize = texDetails.width * texDetails.height * 4 * sizeof(float);
  }
  else if(is_dds_file(f))
  {
    dds = true;
  }
  else
  {
    int ignore = 0;
    int ret = stbi_info_from_file(f, (int *)&texDetails.width, (int *)&texDetails.height, &ignore);

    // just in case (we shouldn't have come in here if this weren't true), make sure
    // the format is supported
    if(ret == 0 || texDetails.width == 0 || texDetails.width == ~0U || texDetails.height == 0 ||
       texDetails.height == ~0U)
    {
      FileIO::fclose(f);
      return;
    }

    texDetails.format = rgba8_unorm;

    data = stbi_load_from_file(f, (int *)&texDetails.width, (int *)&texDetails.height, &ignore, 4);
    datasize = texDetails.width * texDetails.height * 4 * sizeof(byte);
  }

  // if we don't have data at this point (and we're not a dds file) then the
  // file was corrupted and we failed to load it
  if(!dds && data == NULL)
  {
    FileIO::fclose(f);
    return;
  }

  m_FrameRecord.frameInfo.initDataSize = 0;
  m_FrameRecord.frameInfo.persistentSize = 0;
  m_FrameRecord.frameInfo.uncompressedFileSize = datasize;

  dds_data read_data = {0};

  if(dds)
  {
    FileIO::fseek64(f, 0, SEEK_SET);
    read_data = load_dds_from_file(f);

    if(read_data.subdata == NULL)
    {
      FileIO::fclose(f);
      return;
    }

    texDetails.cubemap = read_data.cubemap;
    texDetails.arraysize = read_data.slices;
    texDetails.width = read_data.width;
    texDetails.height = read_data.height;
    texDetails.depth = read_data.depth;
    texDetails.mips = read_data.mips;
    texDetails.format = read_data.format;
    if(texDetails.depth > 1)
    {
      texDetails.type = TextureType::Texture3D;
      texDetails.dimension = 3;
    }
    else if(texDetails.cubemap)
    {
      texDetails.type =
          texDetails.arraysize > 1 ? TextureType::TextureCubeArray : TextureType::TextureCube;
      texDetails.dimension = 2;
    }
    else if(texDetails.width > 1)
    {
      texDetails.type =
          texDetails.arraysize > 1 ? TextureType::Texture2DArray : TextureType::Texture2D;
      texDetails.dimension = 2;
    }
    else
    {
      texDetails.type =
          texDetails.arraysize > 1 ? TextureType::Texture1DArray : TextureType::Texture1D;
      texDetails.dimension = 1;
    }

    m_FrameRecord.frameInfo.uncompressedFileSize = 0;
    for(uint32_t i = 0; i < texDetails.arraysize * texDetails.mips; i++)
      m_FrameRecord.frameInfo.uncompressedFileSize += read_data.subsizes[i];
  }

  m_FrameRecord.frameInfo.compressedFileSize = m_FrameRecord.frameInfo.uncompressedFileSize;

  // recreate proxy texture if necessary.
  // we rewrite the texture IDs so that the
  // outside world doesn't need to know about this
  // (we only ever have one texture in the image
  // viewer so we can just set all texture IDs
  // used to that).
  if(m_TextureID != ResourceId())
  {
    if(m_TexDetails.width != texDetails.width || m_TexDetails.height != texDetails.height ||
       m_TexDetails.depth != texDetails.depth || m_TexDetails.cubemap != texDetails.cubemap ||
       m_TexDetails.mips != texDetails.mips || m_TexDetails.arraysize != texDetails.arraysize ||
       m_TexDetails.width != texDetails.width || m_TexDetails.format != texDetails.format)
    {
      m_TextureID = ResourceId();
    }
  }

  if(m_TextureID == ResourceId())
    m_TextureID = m_Proxy->CreateProxyTexture(texDetails);

  if(m_TextureID == ResourceId())
    RDCERR("Couldn't create proxy texture for image file");

  if(!dds)
  {
    m_Proxy->SetProxyTextureData(m_TextureID, 0, 0, data, datasize);
    free(data);
  }
  else
  {
    for(uint32_t i = 0; i < texDetails.arraysize * texDetails.mips; i++)
    {
      m_Proxy->SetProxyTextureData(m_TextureID, i / texDetails.mips, i % texDetails.mips,
                                   read_data.subdata[i], (size_t)read_data.subsizes[i]);

      delete[] read_data.subdata[i];
    }

    delete[] read_data.subdata;
    delete[] read_data.subsizes;
  }

  FileIO::fclose(f);
}
