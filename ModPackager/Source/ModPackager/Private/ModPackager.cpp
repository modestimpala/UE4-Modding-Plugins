#include "ModPackager.h"
#include "ModPackagerSettings.h"

#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Async/Async.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "FileHelpers.h"

#define LOCTEXT_NAMESPACE "ModPackager"

DEFINE_LOG_CATEGORY_STATIC(LogModPackager, Log, All);

namespace ModPackagerInternal
{
	/** Result handed back to the game thread when the worker finishes. */
	struct FResult
	{
		bool bSuccess = false;
		FString Message;
		FString DeployedPakPath;
		FString ModLogPath;
	};

	static FString GetToolPath(const TCHAR* ExeName)
	{
		const FString EngineBin = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries"), TEXT("Win64")));
		return FPaths::Combine(EngineBin, ExeName);
	}

	/** Append a banner line to the per-mod log file. */
	static void LogLine(const FString& ModLogPath, const FString& Line)
	{
		FFileHelper::SaveStringToFile(Line + LINE_TERMINATOR, *ModLogPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), EFileWrite::FILEWRITE_Append);
	}

	/**
	 * Launches Exe with Args, streams stdout/stderr into ModLogPath, blocks until exit.
	 * Returns the process return code, or -1 if it could not be launched.
	 */
	static int32 RunProcessCaptured(const FString& Exe, const FString& Args, const FString& ModLogPath)
	{
		LogLine(ModLogPath, FString::Printf(TEXT("\n==> %s %s\n"), *Exe, *Args));

		void* ReadPipe = nullptr;
		void* WritePipe = nullptr;
		if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
		{
			LogLine(ModLogPath, TEXT("ERROR: failed to create output pipe."));
			return -1;
		}

		FProcHandle Proc = FPlatformProcess::CreateProc(
			*Exe, *Args,
			/*bLaunchDetached=*/false,
			/*bLaunchHidden=*/true,
			/*bLaunchReallyHidden=*/true,
			/*OutProcessID=*/nullptr,
			/*PriorityModifier=*/0,
			/*OptionalWorkingDirectory=*/nullptr,
			/*PipeWriteChild=*/WritePipe);

		if (!Proc.IsValid())
		{
			FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
			LogLine(ModLogPath, FString::Printf(TEXT("ERROR: failed to launch %s"), *Exe));
			return -1;
		}

		while (FPlatformProcess::IsProcRunning(Proc))
		{
			const FString Chunk = FPlatformProcess::ReadPipe(ReadPipe);
			if (!Chunk.IsEmpty())
			{
				FFileHelper::SaveStringToFile(Chunk, *ModLogPath,
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
					&IFileManager::Get(), EFileWrite::FILEWRITE_Append);
			}
			FPlatformProcess::Sleep(0.1f);
		}

		// Drain anything left in the pipe.
		const FString Tail = FPlatformProcess::ReadPipe(ReadPipe);
		if (!Tail.IsEmpty())
		{
			FFileHelper::SaveStringToFile(Tail, *ModLogPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
				&IFileManager::Get(), EFileWrite::FILEWRITE_Append);
		}

		int32 ReturnCode = -1;
		FPlatformProcess::GetProcReturnCode(Proc, &ReturnCode);
		FPlatformProcess::CloseProc(Proc);
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);

		LogLine(ModLogPath, FString::Printf(TEXT("\n<== exit code %d\n"), ReturnCode));
		return ReturnCode;
	}
}

bool FModPackager::IsPackageableModFolder(const FString& ContentPath)
{
	const UModPackagerSettings* Settings = GetDefault<UModPackagerSettings>();
	const FString Root = FString::Printf(TEXT("/Game/%s"), *Settings->ModsRootFolder);

	// A direct child of the mods root (or deeper). Not the root itself.
	return ContentPath.StartsWith(Root + TEXT("/")) && ContentPath != Root;
}

void FModPackager::PackageMod(const FString& ModPackagePath)
{
	using namespace ModPackagerInternal;
	check(IsInGameThread());

	const UModPackagerSettings* Settings = GetDefault<UModPackagerSettings>();

	// ---- Resolve names & paths -------------------------------------------------
	const FString ModName = FPaths::GetCleanFilename(ModPackagePath); // last path component
	if (ModName.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("BadFolder", "Could not determine a mod name from the selected folder."));
		return;
	}

	// "/Game/Mods/AriralChat" -> "Mods/AriralChat"
	FString ModRelUnderGame = ModPackagePath;
	ModRelUnderGame.RemoveFromStart(TEXT("/Game/"));

	const FString AbsModFolder = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectContentDir(), ModRelUnderGame));

	if (!IFileManager::Get().DirectoryExists(*AbsModFolder))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("NoDirFmt", "Mod folder does not exist on disk:\n{0}"),
			FText::FromString(AbsModFolder)));
		return;
	}

	// ---- Resolve tools ---------------------------------------------------------
	const FString UECmd = GetToolPath(TEXT("UE4Editor-Cmd.exe"));
	const FString UnrealPak = GetToolPath(TEXT("UnrealPak.exe"));
	if (!FPaths::FileExists(UECmd) || !FPaths::FileExists(UnrealPak))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("NoToolsFmt", "Could not find engine tools:\n{0}\n{1}"),
			FText::FromString(UECmd), FText::FromString(UnrealPak)));
		return;
	}

	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString ProjectName = FApp::GetProjectName();
	const FString Platform = Settings->TargetPlatform.IsEmpty() ? TEXT("WindowsNoEditor") : Settings->TargetPlatform;

	const FString CookedContentRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Cooked"), Platform, ProjectName, TEXT("Content")));
	const FString CookedModDir = FPaths::Combine(CookedContentRoot, ModRelUnderGame);

	const FString StagingDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ModPackager")));
	const FString StagedPak = FPaths::Combine(StagingDir, ModName + TEXT(".pak"));
	const FString ResponseFile = FPaths::Combine(StagingDir, ModName + TEXT("_filelist.txt"));
	const FString ModLogPath = FPaths::Combine(StagingDir, ModName + TEXT(".log"));

	// ---- Choose deploy directory ----------------------------------------------
	FString DeployDir = Settings->DefaultDeployDirectory;
	if (Settings->bPromptForDeployDirEachRun || DeployDir.IsEmpty())
	{
		IDesktopPlatform* DP = FDesktopPlatformModule::Get();
		const void* ParentHandle = FSlateApplication::IsInitialized()
			? FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr)
			: nullptr;

		FString Picked;
		const bool bPicked = DP && DP->OpenDirectoryDialog(
			ParentHandle,
			FString::Printf(TEXT("Deploy %s.pak to..."), *ModName),
			DeployDir,
			Picked);

		if (!bPicked || Picked.IsEmpty())
		{
			return; // user cancelled
		}
		DeployDir = Picked;
	}
	DeployDir = FPaths::ConvertRelativePathToFull(DeployDir);
	const FString DeployedPak = FPaths::Combine(DeployDir, ModName + TEXT(".pak"));

	// ---- Save dirty content so the cook sees current edits ---------------------
	FEditorFileUtils::SaveDirtyPackages(
		/*bPromptUserToSave=*/false,
		/*bSaveMapPackages=*/false,
		/*bSaveContentPackages=*/true);

	// Fresh staging dir + log.
	IFileManager::Get().MakeDirectory(*StagingDir, /*Tree=*/true);
	IFileManager::Get().Delete(*ModLogPath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
	LogLine(ModLogPath, FString::Printf(TEXT("Mod Packager - %s"), *ModName));
	LogLine(ModLogPath, FString::Printf(TEXT("Mod folder:   %s"), *AbsModFolder));
	LogLine(ModLogPath, FString::Printf(TEXT("Deploy to:    %s"), *DeployedPak));

	// Capture by value for the worker thread.
	const bool bIterative = Settings->bIterativeCook;
	const bool bCompress = Settings->bCompressPak;
	const bool bClean = Settings->bCleanModCookDirFirst;
	const FString ExtraCookerArgs = Settings->ExtraCookerArgs;

	// ---- Background worker -----------------------------------------------------
	// NOTE: the Slate notification is intentionally NOT captured here. Slate's
	// TSharedPtr is not safe to ref-count across threads, so the worker is defined
	// (and launched) before the notification exists, and the notification is only
	// touched on the game thread in the continuation below.
	auto Worker = [=]() -> FResult
		{
			FResult Result;
			Result.ModLogPath = ModLogPath;

			// 1) Optionally clean this mod's previous cooked output.
			if (bClean && IFileManager::Get().DirectoryExists(*CookedModDir))
			{
				IFileManager::Get().DeleteDirectory(*CookedModDir, /*RequireExists=*/false, /*Tree=*/true);
				LogLine(ModLogPath, FString::Printf(TEXT("Cleaned cooked dir: %s"), *CookedModDir));
			}

			// 2) Scoped cook of ONLY this folder (and its dependencies).
			FString CookArgs = FString::Printf(
				TEXT("\"%s\" -run=Cook -TargetPlatform=%s -CookDir=\"%s\" -unversioned -stdout -unattended -nopause -nosplash -NoLogTimes -UTF8Output"),
				*ProjectFile, *Platform, *AbsModFolder);
			if (bIterative)
			{
				CookArgs += TEXT(" -iterate");
			}
			if (!ExtraCookerArgs.IsEmpty())
			{
				CookArgs += TEXT(" ") + ExtraCookerArgs;
			}

			const int32 CookRC = RunProcessCaptured(UECmd, CookArgs, ModLogPath);
			if (CookRC != 0)
			{
				Result.bSuccess = false;
				Result.Message = FString::Printf(TEXT("Cook failed (exit %d). See log."), CookRC);
				return Result;
			}

			// 3) Gather cooked files for this mod and build the UnrealPak response file.
			TArray<FString> CookedFiles;
			IFileManager::Get().FindFilesRecursive(CookedFiles, *CookedModDir, TEXT("*.*"),
				/*Files=*/true, /*Directories=*/false);
			if (CookedFiles.Num() == 0)
			{
				Result.bSuccess = false;
				Result.Message = TEXT("Cook produced no files for this folder. See log.");
				return Result;
			}

			FString Response;
			for (const FString& File : CookedFiles)
			{
				FString AbsFile = FPaths::ConvertRelativePathToFull(File);
				// Path of the cooked file relative to the cooked Content root.
				FString RelToContent = AbsFile;
				if (!RelToContent.RemoveFromStart(CookedContentRoot + TEXT("/")))
				{
					FPaths::MakePathRelativeTo(RelToContent, *(CookedContentRoot + TEXT("/")));
				}
				RelToContent.ReplaceInline(TEXT("\\"), TEXT("/"));

				// Mount path the shipping game expects:  ../../../<Project>/Content/<rel>
				const FString Mount = FString::Printf(TEXT("../../../%s/Content/%s"), *ProjectName, *RelToContent);
				Response += FString::Printf(TEXT("\"%s\" \"%s\"") LINE_TERMINATOR, *AbsFile, *Mount);
			}

			if (!FFileHelper::SaveStringToFile(Response, *ResponseFile,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				Result.bSuccess = false;
				Result.Message = TEXT("Failed to write UnrealPak response file.");
				return Result;
			}
			LogLine(ModLogPath, FString::Printf(TEXT("Packing %d cooked files."), CookedFiles.Num()));

			// 4) Build the pak.
			IFileManager::Get().Delete(*StagedPak, /*RequireExists=*/false, /*EvenReadOnly=*/true);
			FString PakArgs = FString::Printf(TEXT("\"%s\" -create=\"%s\""), *StagedPak, *ResponseFile);
			if (bCompress)
			{
				PakArgs += TEXT(" -compress");
			}

			const int32 PakRC = RunProcessCaptured(UnrealPak, PakArgs, ModLogPath);
			if (PakRC != 0 || !FPaths::FileExists(StagedPak))
			{
				Result.bSuccess = false;
				Result.Message = FString::Printf(TEXT("UnrealPak failed (exit %d). See log."), PakRC);
				return Result;
			}

			// 5) Deploy: copy + rename to <DeployDir>/<ModName>.pak
			IFileManager::Get().MakeDirectory(*DeployDir, /*Tree=*/true);
			if (IFileManager::Get().Copy(*DeployedPak, *StagedPak, /*Replace=*/true, /*EvenIfReadOnly=*/true)
				!= COPY_OK)
			{
				Result.bSuccess = false;
				Result.Message = FString::Printf(TEXT("Built pak but failed to copy to:\n%s"), *DeployedPak);
				return Result;
			}

			const int64 PakSize = IFileManager::Get().FileSize(*DeployedPak);
			LogLine(ModLogPath, FString::Printf(TEXT("Deployed: %s (%.2f MB)"), *DeployedPak, PakSize / (1024.0 * 1024.0)));

			Result.bSuccess = true;
			Result.DeployedPakPath = DeployedPak;
			Result.Message = FString::Printf(TEXT("%s.pak ready (%.2f MB)"), *ModName, PakSize / (1024.0 * 1024.0));
			return Result;
		};

	TFuture<FResult> Future = Async(EAsyncExecution::Thread, MoveTemp(Worker));

	// ---- Progress notification (game thread) -----------------------------------
	FNotificationInfo Info(FText::Format(
		LOCTEXT("PackagingFmt", "Packaging mod \"{0}\"..."), FText::FromString(ModName)));
	Info.bFireAndForget = false;
	Info.bUseThrobber = true;
	Info.ExpireDuration = 6.0f;
	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(SNotificationItem::CS_Pending);
	}

	// Continuation runs when the worker completes (worker thread), or inline on the
	// game thread if the worker already finished. Move the notification into the
	// game-thread task so its ref-count is never touched concurrently.
	MoveTemp(Future).Next([Notification](FResult Result) mutable
		{
			AsyncTask(ENamedThreads::GameThread, [Note = MoveTemp(Notification), Result]()
			{
				const TSharedPtr<SNotificationItem>& Notification = Note;
				if (Notification.IsValid())
				{
					Notification->SetText(FText::FromString(Result.Message));
					Notification->SetCompletionState(
						Result.bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
					Notification->SetExpireDuration(Result.bSuccess ? 8.0f : 12.0f);

					// Clickable hyperlink: open the deployed folder, or the log on failure.
					const FString LinkTarget = Result.bSuccess
						? FPaths::GetPath(Result.DeployedPakPath)
						: Result.ModLogPath;
					Notification->SetHyperlink(
						FSimpleDelegate::CreateLambda([LinkTarget]()
						{
							FPlatformProcess::ExploreFolder(*LinkTarget);
						}),
						Result.bSuccess
							? LOCTEXT("OpenFolder", "Open folder")
							: LOCTEXT("OpenLog", "Open log"));

					Notification->ExpireAndFadeout();
				}

				if (Result.bSuccess)
				{
					UE_LOG(LogModPackager, Log, TEXT("%s"), *Result.Message);
				}
				else
				{
					UE_LOG(LogModPackager, Error, TEXT("%s (log: %s)"), *Result.Message, *Result.ModLogPath);
				}
			});
		});
}

#undef LOCTEXT_NAMESPACE
