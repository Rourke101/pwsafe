/*
* Copyright (c) 2003-2017 Rony Shapiro <ronys@pwsafe.org>.
* All rights reserved. Use of the code is allowed under the
* Artistic License 2.0 terms, as specified in the LICENSE file
* distributed with this code, or available from
* http://www.opensource.org/licenses/artistic-license-2.0.php
*/

#include <iostream>
#include <sstream>
#include <getopt.h>
#include <string>
#include <map>

#include "./search.h"
#include "./argutils.h"
#include "./searchaction.h"
#include "./strutils.h"
#include "./diff.h"
#include "./safeutils.h"

#include "../../core/PWScore.h"
#include "os/file.h"
#include "core/PWSdirs.h"
#include "core/UTF8Conv.h"
#include "core/Report.h"
#include "core/XML/XMLDefs.h"
#include "../../core/core.h"

using namespace std;

int SaveCore(PWScore &core, const UserArgs &);

static int ImportText(PWScore &core, const StringX &fname);
static int ImportXML(PWScore &core, const StringX &fname);
static int Import(PWScore &core, const UserArgs &ua);
static int Export(PWScore &core, const UserArgs &ua);
static int Search(PWScore &core, const UserArgs &ua);
static int SaveAfterSearch(PWScore &core, const UserArgs &ua);

// These are the new operations. Each returns the code to exit with
static int AddEntry(PWScore &core, const UserArgs &ua);
static int CreateNewSafe(PWScore &core, const StringX& filename);


//-----------------------------------------------------------------

using pre_op_fn = function<int(PWScore &, const StringX &)>;
using main_op_fn = function<int(PWScore &, const UserArgs &)>;
using post_op_fn = function<int(PWScore &, const UserArgs &)>;

auto null_op = [](PWScore &, const UserArgs &)-> int{ return PWScore::SUCCESS;};

struct pws_op {
  pre_op_fn pre_op;
  main_op_fn main_op;
  post_op_fn post_op;
};


const map<UserArgs::OpType, pws_op> pws_ops = {
  { UserArgs::Import,     {OpenCore,        Import,     SaveCore}},
  { UserArgs::Export,     {OpenCore,        Export,     null_op}},
  { UserArgs::CreateNew,  {CreateNewSafe,   null_op,    SaveCore}},
  { UserArgs::Add,        {OpenCore,        AddEntry,   SaveCore}},
  { UserArgs::Search,     {OpenCore,        Search,     SaveAfterSearch}},
  { UserArgs::Diff,       {OpenCore,        Diff,       null_op}}
};


static void usage(char *pname)
{
  cerr << "Usage: " << pname << " safe --imp[=file] --text|--xml" << endl
       << "\t safe --exp[=file] --text|--xml" << endl
  << "\t safe --new" << endl
  << "\t safe --add=filed=value,field=value,..." << endl
  << "\t safe --search=<search text> [--ignore-case] [--subset=<Field><OP><string>[/iI]] [--fields=<comma-separated fieldnames>]"
  << "[--delete|--update:Field1=Value1,Field2=Value2,..|--print] [--yes]" << endl
  << "\t\t where OP is one of ==, !==, ^= !^=, $=, !$=, ~=, !~=" << endl
  << "\t\t\t = => exactly similar" << endl
  << "\t\t\t ^ => begins-with" << endl
  << "\t\t\t $ => ends with"
  << "\t\t\t ^ => contains"
  << "\t\t\t ! => negation" << endl
  << "\t\t a trailing /i or /I at the end of subset string makes the operation case insensitive or sensitive respectively" << endl
  << "\t safe --diff=<othersafe> [--ignore-case] [--subset=<Field><OP><string>[/iI]] [--fields=<comma-separated fieldnames>]"
  << "\t\t[-u | -c | -s ] [ --diffprog=path]" << endl;
}

bool parseArgs(int argc, char *argv[], UserArgs &ua)
{
  if (argc < 3) // must give us a safe and an operation
    return false;

  Utf82StringX(argv[1], ua.safe);

  while (1) {
    int option_index = 0;
    static struct option long_options[] = {
      // name, has_arg, flag, val
      {"import",      optional_argument,  0, 'i'},
      {"export",      optional_argument,  0, 'e'},
      {"text",        no_argument,        0, 't'},
      {"xml",         no_argument,        0, 'x'},
      {"create",      no_argument,        0, 'c'},
      {"new",         no_argument,        0, 'c'},
      {"search",      required_argument,  0, 's'},
      {"subset",      required_argument,  0, 'b'},
      {"fields",      required_argument,  0, 'f'},
      {"ignore-case", optional_argument,  0, 'o'},
      {"add",         required_argument,  0, 'a'},
      {"update",      required_argument,  0, 'u'},
      {"print",       no_argument,        0, 'p'},
      {"remove",      no_argument,        0, 'r'},
      {"delete",      no_argument,        0, 'r'},
      {"yes",         no_argument,        0, 'y'},
      {"diff",        required_argument,  0, 'd'},
      {"unified",     no_argument,        0, 'g'},
      {"context",     no_argument,        0, 'j'},
      {"sidebyside",  no_argument,        0, 'k'},
      {"dry-run",     no_argument,        0, 'n'},
      {0, 0, 0, 0}
    };

    int c = getopt_long(argc-1, argv+1, "i::e::txcs:b:f:oa:u:pryd:gjkn",
                        long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'i':
      if (ua.Operation == UserArgs::Unset)
        ua.Operation = UserArgs::Import;
      else
        return false;
      if (optarg) Utf82StringX(optarg, ua.fname);
      break;
    case 'e':
      if (ua.Operation == UserArgs::Unset)
        ua.Operation = UserArgs::Export;
      else
        return false;
      if (optarg) Utf82StringX(optarg, ua.fname);
      break;
    case 'x':
      if (ua.Format == UserArgs::Unknown)
        ua.Format = UserArgs::XML;
      else
        return false;
      break;
    case 't':
      if (ua.Format == UserArgs::Unknown)
        ua.Format = UserArgs::Text;
      else
        return false;
      break;
    case 'c':
      if (ua.Operation == UserArgs::Unset)
        ua.Operation = UserArgs::CreateNew;
      else
        return false;
      break;

    case 's':
      if (ua.Operation == UserArgs::Unset) {
        ua.Operation = UserArgs::Search;
        assert(optarg);
        ua.opArg = Utf82wstring(optarg);
        break;
      }
      else
        return false;

    case 'd':
      if (ua.Operation == UserArgs::Unset) {
        ua.Operation = UserArgs::Diff;
        assert(optarg);
        ua.opArg = Utf82wstring(optarg);
        break;
      }
      else
        return false;

    case 'b':
        assert(optarg);
        ua.SetSubset(Utf82wstring(optarg));
        break;

    case 'f':
        assert(optarg);
        ua.SetFields(Utf82wstring(optarg));
        break;

    case 'o':
        if (optarg && std::regex_match(optarg, std::regex("yes|true", std::regex::icase)))
          ua.ignoreCase = true;
        break;

    case 'a':
        ua.Operation = UserArgs::Add;
        assert(optarg);
        ua.opArg = Utf82wstring(optarg);
        break;

    case 'y':
        ua.confirmed = true;
        break;

    case 'r':
        ua.SearchAction = UserArgs::Delete;
        break;

    case 'p':
        ua.SearchAction = UserArgs::Print;
        break;

    case 'u':
        ua.SearchAction = UserArgs::Update;
        assert(optarg);
        ua.opArg2 = Utf82wstring(optarg);
        break;

      case 'g':
        ua.dfmt = UserArgs::DiffFmt::Unified;
        break;

      case 'j':
        ua.dfmt = UserArgs::DiffFmt::Context;
        break;

      case 'k':
        ua.dfmt = UserArgs::DiffFmt::SideBySide;
        break;

      case 'n':
        ua.dry_run = true;
        break;

    default:
      cerr << "Unknown option: " << char(c) << endl;
      return false;
    }
    if (ua.fname.empty())
      ua.fname = (ua.Format == UserArgs::XML) ? L"file.xml" : L"file.txt";
  }
  return true;
}


int main(int argc, char *argv[])
{
  UserArgs ua;
  if (!parseArgs(argc, argv, ua)) {
    usage(argv[0]);
    return 1;
  }

  int status = 1;
  auto itr = pws_ops.find(ua.Operation);
  if (itr != pws_ops.end()) {
    PWScore core;
    try {
      status = itr->second.pre_op(core, ua.safe);
      if ( status == PWScore::SUCCESS) {
        status = itr->second.main_op(core, ua);
        if (status == PWScore::SUCCESS)
          status = itr->second.post_op(core, ua);
      }
    }
    catch(const exception &e) {
      cerr << e.what() << endl;
      status = PWScore::FAILURE;
    }

    core.UnlockFile(ua.safe.c_str());
    return status;
  }
  cerr << "No main operation specified" << endl;
  return status;
}

//-----------------------------------------------------------------
int Import(PWScore &core, const UserArgs &ua)
{
  return ua.Format == UserArgs::XML?
      ImportXML(core, ua.fname): ImportText(core, ua.fname);
}

static int
ImportText(PWScore &core, const StringX &fname)
{
  int numImported(0), numSkipped(0), numPWHErrors(0), numRenamed(0), numNoPolicy(0);
  std::wstring strError;
  wchar_t delimiter = L' ';
  wchar_t fieldSeparator = L'\t';
  StringX ImportedPrefix;
  bool bImportPSWDsOnly = false;

  // Create report as we go
  CReport rpt;
  rpt.StartReport(L"Import_Text", core.GetCurFile().c_str());
  wstring str(L"Text file being imported: ");
  str += core.GetCurFile().c_str();
  rpt.WriteLine(str.c_str());
  rpt.WriteLine();

  Command *pcmd = NULL;
  int rc = core.ImportPlaintextFile(ImportedPrefix, fname, fieldSeparator,
                                    delimiter, bImportPSWDsOnly,
                                    strError,
                                    numImported, numSkipped,
                                    numPWHErrors, numRenamed, numNoPolicy,
                                    rpt, pcmd);
  switch (rc) {
  case PWScore::CANT_OPEN_FILE:
  case PWScore::INVALID_FORMAT:
  case PWScore::FAILURE:
    delete pcmd;
    break;
  case PWScore::SUCCESS:
  case PWScore::OK_WITH_ERRORS:
    // deliberate fallthrough
  default:
    {
      rc = core.Execute(pcmd);

      rpt.WriteLine();
      wstring op(bImportPSWDsOnly ? L"Updated " : L"Imported ");
      wstring entries(numImported == 1 ? L" entry" : L" entries");
      wostringstream os;
      os << op << numImported << entries;
      rpt.WriteLine(os.str().c_str());

      if (numSkipped != 0) {
        wostringstream oss;
        entries = (numSkipped == 1 ?  L" entry" : L" entries");
        oss << L"\nSkipped " << numSkipped << entries;
        rpt.WriteLine(oss.str().c_str());
      }

      if (numPWHErrors != 0) {
        wostringstream oss;
        entries = (numPWHErrors == 1 ?  L" entry" : L" entries");
        oss << L"\nwith Password History errors" << numPWHErrors << entries;
        rpt.WriteLine(oss.str().c_str());
      }

      if (numRenamed != 0) {
        wostringstream oss;
        entries = (numRenamed == 1 ?  L" entry" : L" entries");
        oss << L"\nRenamed " << numRenamed << entries;
        rpt.WriteLine(oss.str().c_str());
      }
      break;
    }
  } // switch
  rpt.EndReport();
  return rc;
}

static int
ImportXML(PWScore &core, const StringX &fname)
{
  const std::wstring XSDfn(L"pwsafe.xsd");
  std::wstring XSDFilename = PWSdirs::GetXMLDir() + XSDfn;

#if USE_XML_LIBRARY == MSXML || USE_XML_LIBRARY == XERCES
  if (!pws_os::FileExists(XSDFilename)) {
    wcerr << L"Can't find XML Schema Definition file"
          << XSDFilename << endl
          << L"Can't import without it." << endl;
    return PWScore::XML_FAILED_IMPORT;
  }
#endif

  std::wstring ImportedPrefix;
  std::wstring strXMLErrors, strSkippedList, strPWHErrorList, strRenameList;
  int numValidated(0), numImported(0), numSkipped(0), numRenamed(0), numPWHErrors(0);
  int numNoPolicy(0), numRenamedPolicies(0), numShortcutsRemoved(0), numEmptyGroupsRemoved(0);
  bool bImportPSWDsOnly = false;

  
  // Create report as we go
  CReport rpt;
  std::wstring str_text;
  rpt.StartReport(L"Import_XML", core.GetCurFile().c_str());
  str_text = L"XML file being imported: ";
  str_text += fname.c_str();
  rpt.WriteLine(str_text);
  rpt.WriteLine();
  Command *pcmd = NULL;

  int rc = core.ImportXMLFile(ImportedPrefix.c_str(), fname.c_str(),
                              XSDFilename.c_str(), bImportPSWDsOnly,
                              strXMLErrors, strSkippedList, strPWHErrorList,
                              strRenameList, numValidated, numImported,
                              numSkipped, numPWHErrors, numRenamed,
                              numNoPolicy, numRenamedPolicies, numShortcutsRemoved, numEmptyGroupsRemoved,
                              rpt, pcmd);

  switch (rc) {
  case PWScore::XML_FAILED_VALIDATION:
    rpt.WriteLine(strXMLErrors.c_str());
    str_text = L"File ";
    str_text += fname.c_str();
    str_text += L" failed to validate.";
    delete pcmd;
    break;
  case PWScore::XML_FAILED_IMPORT:
    rpt.WriteLine(strXMLErrors.c_str());
    str_text = L"File ";
    str_text += fname.c_str();
    str_text += L" validated but hadd errors during import.";
    delete pcmd;
    break;
  case PWScore::SUCCESS:
  case PWScore::OK_WITH_ERRORS:
    if (pcmd != NULL)
      rc = core.Execute(pcmd);

    if (!strXMLErrors.empty() ||
        numRenamed > 0 || numPWHErrors > 0) {
      wstring csErrors;
      if (!strXMLErrors.empty())
        csErrors = strXMLErrors + L"\n";

      if (!csErrors.empty()) {
        rpt.WriteLine(csErrors.c_str());
      }

      wstring cs_renamed, cs_PWHErrors, cs_skipped;
      if (numSkipped > 0) {
        rpt.WriteLine(wstring(L"The following records were skipped:"));
        wostringstream oss;
        oss << L" / skipped " << numSkipped;
        cs_skipped = oss.str();
        rpt.WriteLine(strSkippedList.c_str());
        rpt.WriteLine();
      }
      if (numPWHErrors > 0) {
        rpt.WriteLine(wstring(L"The following records had errors in their Password History:"));
        wostringstream oss;
        oss << L" / with Password History errors " << numPWHErrors;
        cs_PWHErrors = oss.str();
        rpt.WriteLine(strPWHErrorList.c_str());
        rpt.WriteLine();
      }
      if (numRenamed > 0) {
        rpt.WriteLine(wstring(L"The following records were renamed as an entry already exists in your database or in the Import file:"));
        wostringstream oss;
        oss << L" / renamed " << numRenamed;
        cs_renamed = oss.str();
        rpt.WriteLine(strRenameList.c_str());
        rpt.WriteLine();
      }

      wostringstream os2;
      os2 << L"File " << fname << L" was imported (entries validated"
          << numValidated << L" / imported " << numImported
          << cs_skipped <<  cs_renamed << cs_PWHErrors;
      str_text = os2.str();
    } else {
      const wstring validate(numValidated == 1 ? L" entry" : L" entries");
      const wstring import(numImported == 1 ? L" entry" : L" entries");
      wostringstream oss;
      oss << L"Validated " << numValidated << validate << L'\t'
          << L"Imported " << numImported << import << endl;
      str_text = oss.str();
    }

    break;
  default:
    ASSERT(0);
  } // switch

    // Finish Report
  rpt.WriteLine(str_text);
  rpt.EndReport();
  return rc;
}

static int Export(PWScore &core, const UserArgs &ua)
{
  CItemData::FieldBits all(~0L);
  int N;
  return ua.Format == UserArgs::XML?
    core.WriteXMLFile(ua.fname, all, L"", 0, 0, L' ', N):
      core.WritePlaintextFile(ua.fname, all, L"", 0, 0, L' ', N);
}

static int CreateNewSafe(PWScore &core, const StringX& filename)
{
    if ( pws_os::FileExists(filename.c_str()) ) {
        cerr << filename << " - already exists" << endl;
        exit(1);
    }

    const StringX passkey = GetNewPassphrase();
    core.SetCurFile(filename);
    core.NewFile(passkey);

    return PWScore::SUCCESS;
}


int AddEntry(PWScore &core, const UserArgs &ua)
{
  const wstring fieldValues{ua.opArg};

  CItemData item;
  item.CreateUUID();
  int status = PWScore::SUCCESS;
  Split(fieldValues, L"[,;]", [&item, &status](const wstring &nameval) {
    std::wsmatch m;
    if (std::regex_match(nameval, m, std::wregex(L"([^=]+)=(.+)"))) {
      item.SetFieldValue(String2FieldType(m.str(1)), std2stringx(m.str(2)));
    }
    else {
      wcerr << L"Could not parse field value " << endl;
      status = PWScore::FAILURE;
    }
  });

  if (status == PWScore::SUCCESS)
    status = core.Execute(AddEntryCommand::Create(&core, item));
  if (status == PWScore::SUCCESS)
    status = core.WriteCurFile();

  return status;
}

int Search(PWScore &core, const UserArgs &ua)
{
  unique_ptr<SearchAction> sa(CreateSearchAction(ua.SearchAction, &core, ua.opArg2, ua.confirmed));
  SearchForEntries(core, ua.opArg, ua.ignoreCase, ua.subset, ua.fields, *sa);
  return sa->Execute();
}

int SaveAfterSearch(PWScore &core, const UserArgs &ua)
{
  if ( (ua.SearchAction == UserArgs::Update ||
        ua.SearchAction == UserArgs::Delete) && core.IsChanged() ) {
    return core.WriteCurFile();
  }
  return PWScore::SUCCESS;
}

int SaveCore(PWScore &core, const UserArgs &ua)
{
  if (!ua.dry_run)
    return core.WriteCurFile();

  return PWScore::SUCCESS;
}
