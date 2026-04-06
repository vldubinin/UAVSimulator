#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/Entity/PolarRow.h"

/**
 * Low-level I/O and external-process helpers for the aerodynamic analysis toolchain.
 * Handles file system operations, polar file parsing, and Python/XFoil/OpenVSP execution.
 */
class UAVSIMULATOR_API AerodynamicToolRunner
{
public:
	/** Return (and create if missing) the temporary work directory for tool outputs. */
	static FString GetOrCreateWorkDir();

	/** Delete all contents of the temporary work directory. */
	static bool CleanWorkDir();

	/** Convert a project-relative path to an absolute path for external apps. */
	static FString ToAbsolutePath(FString Path);

	/** Copy a file into the temporary work directory under the given name. Returns the destination path. */
	static FString CopyToWorkDir(FString SourcePath, FString Name);

	/** Serialize a polar map to a .dat file in the work directory. Returns the output file path. */
	static FString SavePolarFile(TMap<float, FPolarRow> Polar);

	/** Execute a Python script via the UE PythonScriptPlugin. Returns true on success. */
	static bool RunPythonScript(FString Command);

	/**
	 * Parse a whitespace-delimited polar text file.
	 * @param AoAIdx, ClIdx, CdIdx, CmIdx — zero-based column indices for AoA, CL, CD, CM.
	 */
	static TMap<float, FPolarRow> ParsePolarFile(FString Path, int32 AoAIdx, int32 ClIdx, int32 CdIdx, int32 CmIdx);

	/** Derive the airplane folder name from a UObject's class package path. */
	static FString GetOwnerFolderName(UObject* ContextObject);
};
