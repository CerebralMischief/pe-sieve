#include "workingset_scanner.h"
#include "module_data.h"
#include "artefact_scanner.h"

#include "../utils/path_converter.h"
#include "../utils/workingset_enum.h"
#include "../utils/artefacts_util.h"

bool WorkingSetScanner::isCode(MemPageData &memPageData)
{
	if (!memPage.load()) {
		return false;
	}
	return is_code(memPageData.getLoadedData(), memPageData.getLoadedSize());
}

bool WorkingSetScanner::isExecutable(MemPageData &memPageData)
{
	bool is_any_exec = false;
	if (memPage.mapping_type == MEM_IMAGE)
	{
		is_any_exec = (memPage.protection & SECTION_MAP_EXECUTE)
			|| (memPage.protection & SECTION_MAP_EXECUTE_EXPLICIT)
			|| (memPage.initial_protect & SECTION_MAP_EXECUTE)
			|| (memPage.initial_protect & SECTION_MAP_EXECUTE_EXPLICIT);

		if (is_any_exec) return true;
	}
	is_any_exec = (memPage.initial_protect & PAGE_EXECUTE_READWRITE)
		|| (memPage.initial_protect & PAGE_EXECUTE_READ)
		|| (memPage.initial_protect & PAGE_EXECUTE)
		|| (memPage.initial_protect & PAGE_EXECUTE_WRITECOPY)
		|| (memPage.protection & PAGE_EXECUTE_READWRITE)
		|| (memPage.protection & PAGE_EXECUTE_READ)
		|| (memPage.protection & PAGE_EXECUTE)
		|| (memPage.protection & PAGE_EXECUTE_WRITECOPY);
	if (is_any_exec) return true;

	if (this->scanData) {
		is_any_exec = isPotentiallyExecutable(memPageData);
	}
	return is_any_exec;
}

bool WorkingSetScanner::isPotentiallyExecutable(MemPageData &memPageData)
{
	bool is_any_exec = false;
	if (!memPage.is_dep_enabled) {
		//DEP is disabled, check also pages that are readable
		is_any_exec = (memPage.protection & PAGE_READWRITE)
			|| (memPage.protection & PAGE_READONLY);
	}
	return is_any_exec;
}

WorkingSetScanReport* WorkingSetScanner::scanExecutableArea(MemPageData &memPageData)
{
	if (!memPage.load()) {
		return nullptr;
	}
	//shellcode found! now examin it with more details:
	ArtefactScanner artefactScanner(this->processHandle, memPage);
	WorkingSetScanReport *my_report = artefactScanner.scanRemote();
	if (my_report) {
		//pe artefacts found
		return my_report;
	}
	if (!this->detectShellcode) {
		// not a PE file, and we are not interested in shellcode, so just finish it here
		return nullptr;
	}
	if (!isCode(memPage)) {
		// shellcode patterns not found
		return nullptr;
	}
	//report about shellcode:
	ULONGLONG region_start = memPage.region_start;
	const size_t region_size = size_t (memPage.region_end - region_start);
	my_report = new WorkingSetScanReport(processHandle, (HMODULE)region_start, region_size, SCAN_SUSPICIOUS);
	my_report->has_pe = false;
	my_report->has_shellcode = true;
	return my_report;
}

WorkingSetScanReport* WorkingSetScanner::scanRemote()
{
	if (!memPage.isInfoFilled() && !memPage.fillInfo()) {
		return nullptr;
	}

	// is the page executable?
	bool is_any_exec = isExecutable(memPage);
	if (!is_any_exec) {
		// probably not interesting
		return nullptr;
	}
	bool is_doppel = false;
	if (memPage.mapping_type == MEM_IMAGE) {
		if (!memPage.hasMappedName()) {
			is_doppel = true;
		} else {
			//probably legit
			return nullptr;
		}
	}
	if (memPage.mapping_type == MEM_MAPPED && memPage.isRealMapping()) {
		//probably legit
		return nullptr;
	}
	WorkingSetScanReport* my_report = nullptr;
	if (is_any_exec) {
#ifdef _DEBUG
		std::cout << std::hex << memPage.start_va << ": Scanning executable area" << std::endl;
#endif
		my_report = this->scanExecutableArea(memPage);
	}
	if (!my_report) {
		return nullptr;
	}
	my_report->is_executable = true;
	my_report->protection = memPage.protection;
	my_report->is_doppel = is_doppel;
	return my_report;
}
