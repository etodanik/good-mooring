// The directory helpers follow Forge's AssetPipeline.cpp (Apache-2.0).
#include "Common/Tools/AssetPipeline/src/AssetPipeline.h"
#include "Common/Utilities/Interfaces/ILog.h"
#include "Common/Utilities/Interfaces/IMemory.h"

void CreateDirectoryForFile(TFResourceDirectory directory, const char* filename)
{
    char parent[TF_FS_MAX_PATH] = {};
    fsGetParentPath(filename, parent);
    fsCreateDirectory(directory, parent, true);
}

void DirectorySearch(TFResourceDirectory directory, const char* subDir, const char* extension, OnFind callback, void* context,
                     bool recursive)
{
    char** files = nullptr;
    int    count = 0;
    fsGetFilesWithExtension(directory, subDir ? subDir : "", extension, &files, &count);
    for (int i = 0; i < count; ++i)
        if (files[i])
            callback(directory, files[i], context);
    tf_free(files);
    if (!recursive)
        return;
    char** folders = nullptr;
    fsGetSubDirectories(directory, subDir ? subDir : "", &folders, &count);
    for (int i = 0; i < count; ++i)
        if (folders[i])
            DirectorySearch(directory, folders[i], extension, callback, context, true);
    tf_free(folders);
}

int main(int argc, const char** argv)
{
    if (argc != 3)
        return 2;
    initMemAlloc("MooringFontTool");
    TFFileSystemInitDesc fs = {};
    fs.mIsTool = true;
    initFileSystem(&fs);
    fsSetPathForResourceDir(pSystemFileIO, TF_RD_FONTS, argv[1]);
    fsSetPathForResourceDir(pSystemFileIO, TF_RD_OTHER_FILES, argv[2]);
    fsSetPathForResourceDir(pSystemFileIO, TF_RD_LOG, argv[2]);
    initLog("MooringFontTool", (LogLevel)(eWARNING | eERROR));
    AssetPipelineParams params = {};
    params.mInFilePath = "Inter-Regular.ttf";
    params.mPathMode = PROCESS_MODE_FILE;
    params.mRDInput = TF_RD_FONTS;
    params.mRDOutput = TF_RD_OTHER_FILES;
    params.mSettings.force = true;
    const bool failed = ProcessFonts(&params, { 64, 4.0f, true });
    exitLog();
    exitFileSystem();
    exitMemAlloc();
    return failed ? 1 : 0;
}
