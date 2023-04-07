// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: deblistparser.cc,v 1.29.2.5 2004/01/06 01:43:44 mdz Exp $
/* ######################################################################
   
   Package Cache Generator - Generator for the cache structure.
   
   This builds the cache structure from the abstract package list parser. 
   
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <apt-pkg/deblistparser.h>
#include <apt-pkg/error.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/crc-16.h>
#include <apt-pkg/md5.h>
#include <apt-pkg/macros.h>

#include <ctype.h>
									/*}}}*/

static debListParser::WordList PrioList[] = {{"important",pkgCache::State::Important},
                       {"required",pkgCache::State::Required},
                       {"standard",pkgCache::State::Standard},
                       {"optional",pkgCache::State::Optional},
	               {"extra",pkgCache::State::Extra},
                       {}};

// ListParser::debListParser - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
debListParser::debListParser(FileFd *File) : Tags(File)
{
   Arch = _config->Find("APT::architecture");
}
									/*}}}*/
// ListParser::UniqFindTagWrite - Find the tag and write a unq string	/*{{{*/
// ---------------------------------------------------------------------
/* */
unsigned long debListParser::FindTagWrite(const char *Tag)
{
   const char *Start;
   const char *Stop;
   if (Section.Find(Tag,Start,Stop) == false)
      return 0;
   return WriteString(Start,Stop - Start);
}
									/*}}}*/
// ListParser::UniqFindTagWrite - Find the tag and write a unq string	/*{{{*/
// ---------------------------------------------------------------------
/* */
unsigned long debListParser::UniqFindTagWrite(const char *Tag)
{
   const char *Start;
   const char *Stop;
   if (Section.Find(Tag,Start,Stop) == false)
      return 0;
   return WriteUniqString(Start,Stop - Start);
}
									/*}}}*/
// ListParser::Package - Return the package name			/*{{{*/
// ---------------------------------------------------------------------
/* This is to return the name of the package this section describes */
string debListParser::Package()
{
   string Result = Section.FindS("Package");
   if (Result.empty() == true)
      _error->Error("Encountered a section with no Package: header");
   return Result;
}
									/*}}}*/
// ListParser::Version - Return the version string			/*{{{*/
// ---------------------------------------------------------------------
/* This is to return the string describing the version in debian form,
   epoch:upstream-release. If this returns the blank string then the 
   entry is assumed to only describe package properties */
string debListParser::Version()
{
   return Section.FindS("Version");
}
									/*}}}*/
// ListParser::NewVersion - Fill in the version structure		/*{{{*/
// ---------------------------------------------------------------------
/* */
bool debListParser::NewVersion(pkgCache::VerIterator Ver)
{
   Ver->Display = FindTagWrite("Name");
   if (Ver->Display == 0)
      Ver->Display = FindTagWrite("Maemo-Display-Name");

   // Parse the section
   Ver->Section = UniqFindTagWrite("Section");
   Ver->Arch = UniqFindTagWrite("Architecture");
   
   // Archive Size
   Ver->Size = (unsigned)Section.FindI("Size");
   
   // Unpacked Size (in K)
   Ver->InstalledSize = (unsigned)Section.FindI("Installed-Size");
   Ver->InstalledSize *= 1024;

   // Priority
   const char *Start;
   const char *Stop;
   if (Section.Find("Priority",Start,Stop) == true)
   {      
      if (GrabWord(srkString(Start,Stop-Start),PrioList,Ver->Priority) == false)
	 Ver->Priority = pkgCache::State::Extra;
   }

   if (ParseDepends(Ver,"Depends",pkgCache::Dep::Depends) == false)
      return false;
   if (ParseDepends(Ver,"Pre-Depends",pkgCache::Dep::PreDepends) == false)
      return false;
   if (ParseDepends(Ver,"Suggests",pkgCache::Dep::Suggests) == false)
      return false;
   if (ParseDepends(Ver,"Recommends",pkgCache::Dep::Recommends) == false)
      return false;
   if (ParseDepends(Ver,"Conflicts",pkgCache::Dep::Conflicts) == false)
      return false;
   if (ParseDepends(Ver,"Breaks",pkgCache::Dep::DpkgBreaks) == false)
      return false;
   if (ParseDepends(Ver,"Replaces",pkgCache::Dep::Replaces) == false)
      return false;
   if (ParseDepends(Ver,"Enhances",pkgCache::Dep::Enhances) == false)
      return false;

   // Obsolete.
   if (ParseDepends(Ver,"Optional",pkgCache::Dep::Suggests) == false)
      return false;
   
   if (ParseProvides(Ver) == false)
      return false;
   
   return true;
}
									/*}}}*/
// ListParser::Description - Return the description string		/*{{{*/
// ---------------------------------------------------------------------
/* This is to return the string describing the package in debian
   form. If this returns the blank string then the entry is assumed to
   only describe package properties */
string debListParser::Description()
{
   srkString description;
   Description(description);
   return description;
}

void debListParser::Description(srkString &Str) {
   const char *Start, *Stop;
   if (!Section.Find("Description", Start, Stop))
      if (!Section.Find(("Description-" + pkgIndexFile::LanguageCode()).c_str(), Start, Stop)) {
         Start = NULL;
         Stop = NULL;
      }
   Str.assign(Start, Stop);
}
                                                                        /*}}}*/
// ListParser::DescriptionLanguage - Return the description lang string	/*{{{*/
// ---------------------------------------------------------------------
/* This is to return the string describing the language of
   description. If this returns the blank string then the entry is
   assumed to describe original description. */
string debListParser::DescriptionLanguage()
{
   const char *Start, *Stop;
   return Section.Find("Description", Start, Stop) ? std::string() : pkgIndexFile::LanguageCode();
}
                                                                        /*}}}*/
// ListParser::Description - Return the description_md5 MD5SumValue	/*{{{*/
// ---------------------------------------------------------------------
/* This is to return the md5 string to allow the check if it is the right
   description. If no Description-md5 is found in the section it will be
   calculated.
 */
MD5SumValue debListParser::Description_md5()
{
   const char *Start;
   const char *Stop;
   if (!Section.Find("Description-md5", Start, Stop))
   {
      MD5Summation md5;
      srkString description;
      Description(description);
      md5.Add((const unsigned char *) description.Start, description.Size);
      md5.Add("\n");
      return md5.Result();
   } else
      return MD5SumValue(srkString(Start, Stop));
}
                                                                        /*}}}*/
// ListParser::UsePackage - Update a package structure			/*{{{*/
// ---------------------------------------------------------------------
/* This is called to update the package with any new information 
   that might be found in the section */
bool debListParser::UsePackage(pkgCache::PkgIterator Pkg,
			       pkgCache::VerIterator Ver)
{
   if (Pkg->Display == 0)
      Pkg->Display = FindTagWrite("Name");
   if (Pkg->Display == 0)
      Pkg->Display = FindTagWrite("Maemo-Display-Name");
   if (Pkg->Section == 0)
      Pkg->Section = UniqFindTagWrite("Section");
   if (Section.FindFlag("Essential",Pkg->Flags,pkgCache::Flag::Essential) == false)
      return false;
   if (Section.FindFlag("Important",Pkg->Flags,pkgCache::Flag::Important) == false)
      return false;

   if (strcmp(Pkg.Name(),"apt") == 0)
      Pkg->Flags |= pkgCache::Flag::Important;
   
   if (ParseStatus(Pkg,Ver) == false)
      return false;

   if (Pkg->TagList == 0)
      if (ParseTag(Pkg) == false)
         return false;

   return true;
}
									/*}}}*/
// ListParser::VersionHash - Compute a unique hash for this version	/*{{{*/
// ---------------------------------------------------------------------
/* */
unsigned short debListParser::VersionHash()
{
   const char *Sections[] ={"Installed-Size",
                            "Depends",
                            "Pre-Depends",
//                            "Suggests",
//                            "Recommends",
                            "Conflicts",
                            "Breaks",
                            "Replaces",0};
   unsigned long Result = INIT_FCS;
   char S[1024];
   for (const char **I = Sections; *I != 0; I++)
   {
      const char *Start;
      const char *End;
      if (Section.Find(*I,Start,End) == false || End - Start >= (signed)sizeof(S))
	 continue;
      
    {
      /* Strip out any spaces from the text, this undoes dpkgs reformatting
         of certain fields. dpkg also has the rather interesting notion of
         reformatting depends operators < -> <= */
      char *I = S;
      for (; Start != End; Start++)
      {
	 if (isspace(*Start) == 0)
	    *I++ = tolower_ascii(*Start);
	 if (*Start == '<' && Start[1] != '<' && Start[1] != '=')
	    *I++ = '=';
	 if (*Start == '>' && Start[1] != '>' && Start[1] != '=')
	    *I++ = '=';
      }

      Result = AddCRC16(Result,S,I - S);
    }
   }
   
   return Result;
}
									/*}}}*/
// ListParser::ParseStatus - Parse the status field			/*{{{*/
// ---------------------------------------------------------------------
/* Status lines are of the form,
     Status: want flag status
   want = unknown, install, hold, deinstall, purge
   flag = ok, reinstreq, hold, hold-reinstreq
   status = not-installed, unpacked, half-configured,
            half-installed, config-files, post-inst-failed, 
            removal-failed, installed
   
   Some of the above are obsolete (I think?) flag = hold-* and 
   status = post-inst-failed, removal-failed at least.
 */
bool debListParser::ParseStatus(pkgCache::PkgIterator Pkg,
				pkgCache::VerIterator Ver)
{
   const char *Start;
   const char *Stop;
   if (Section.Find("Status",Start,Stop) == false)
      return true;
   
   // Isolate the first word
   const char *I = Start;
   for(; I < Stop && *I != ' '; I++);
   if (I >= Stop || *I != ' ')
      return _error->Error("Malformed Status line");

   // Process the want field
   WordList WantList[] = {{"unknown",pkgCache::State::Unknown},
                          {"install",pkgCache::State::Install},
                          {"hold",pkgCache::State::Hold},
                          {"deinstall",pkgCache::State::DeInstall},
                          {"purge",pkgCache::State::Purge},
                          {}};
   if (GrabWord(srkString(Start,I-Start),WantList,Pkg->SelectedState) == false)
      return _error->Error("Malformed 1st word in the Status line");

   // Isloate the next word
   I++;
   Start = I;
   for(; I < Stop && *I != ' '; I++);
   if (I >= Stop || *I != ' ')
      return _error->Error("Malformed status line, no 2nd word");

   // Process the flag field
   WordList FlagList[] = {{"ok",pkgCache::State::Ok},
                          {"reinstreq",pkgCache::State::ReInstReq},
                          {"hold",pkgCache::State::HoldInst},
                          {"hold-reinstreq",pkgCache::State::HoldReInstReq},
                          {}};
   if (GrabWord(srkString(Start,I-Start),FlagList,Pkg->InstState) == false)
      return _error->Error("Malformed 2nd word in the Status line");

   // Isloate the last word
   I++;
   Start = I;
   for(; I < Stop && *I != ' '; I++);
   if (I != Stop)
      return _error->Error("Malformed Status line, no 3rd word");

   // Process the flag field
   WordList StatusList[] = {{"not-installed",pkgCache::State::NotInstalled},
                            {"unpacked",pkgCache::State::UnPacked},
                            {"half-configured",pkgCache::State::HalfConfigured},
                            {"installed",pkgCache::State::Installed},
                            {"half-installed",pkgCache::State::HalfInstalled},
                            {"config-files",pkgCache::State::ConfigFiles},
                            {"triggers-awaited",pkgCache::State::TriggersAwaited},
                            {"triggers-pending",pkgCache::State::TriggersPending},
                            {"post-inst-failed",pkgCache::State::HalfConfigured},
                            {"removal-failed",pkgCache::State::HalfInstalled},
                            {}};
   if (GrabWord(srkString(Start,I-Start),StatusList,Pkg->CurrentState) == false)
      return _error->Error("Malformed 3rd word in the Status line");

   /* A Status line marks the package as indicating the current
      version as well. Only if it is actually installed.. Otherwise
      the interesting dpkg handling of the status file creates bogus 
      entries. */
   if (!(Pkg->CurrentState == pkgCache::State::NotInstalled ||
	 Pkg->CurrentState == pkgCache::State::ConfigFiles))
   {
      if (Ver.end() == true)
	 _error->Warning("Encountered status field in a non-version description");
      else
	 Pkg->CurrentVer = Ver.Index();
   }
   
   return true;
}

const char *debListParser::ConvertRelation(const char *I,unsigned int &Op)
{
   // Determine the operator
   switch (*I)
   {
      case '<':
      I++;
      if (*I == '=')
      {
	 I++;
	 Op = pkgCache::Dep::LessEq;
	 break;
      }
      
      if (*I == '<')
      {
	 I++;
	 Op = pkgCache::Dep::Less;
	 break;
      }
      
      // < is the same as <= and << is really Cs < for some reason
      Op = pkgCache::Dep::LessEq;
      break;
      
      case '>':
      I++;
      if (*I == '=')
      {
	 I++;
	 Op = pkgCache::Dep::GreaterEq;
	 break;
      }
      
      if (*I == '>')
      {
	 I++;
	 Op = pkgCache::Dep::Greater;
	 break;
      }
      
      // > is the same as >= and >> is really Cs > for some reason
      Op = pkgCache::Dep::GreaterEq;
      break;
      
      case '=':
      Op = pkgCache::Dep::Equals;
      I++;
      break;
      
      // HACK around bad package definitions
      default:
      Op = pkgCache::Dep::Equals;
      break;
   }
   return I;
}

									/*}}}*/
// ListParser::ParseDepends - Parse a dependency element		/*{{{*/
// ---------------------------------------------------------------------
/* This parses the dependency elements out of a standard string in place,
   bit by bit. */
const char *debListParser::ParseDepends(const char *Start,const char *Stop,
					string &Package,string &Ver,
					unsigned int &Op, bool ParseArchFlags)
{
   srkString cPackage, cVer;
   const char *Value = ParseDepends(Start, Stop, cPackage, cVer, Op, ParseArchFlags);
   Package = cPackage;
   Ver = cVer;
   return Value;
}

const char *debListParser::ParseDepends(const char *Start,const char *Stop,
					srkString &Package,srkString &Ver,
					unsigned int &Op, bool ParseArchFlags)
{
   // Strip off leading space
   for (;Start != Stop && isspace(*Start) != 0; Start++);
   
   // Parse off the package name
   const char *I = Start;
   for (;I != Stop && isspace(*I) == 0 && *I != '(' && *I != ')' &&
	*I != ',' && *I != '|'; I++);
   
   // Malformed, no '('
   if (I != Stop && *I == ')')
      return 0;

   if (I == Start)
      return 0;
   
   // Stash the package name
   Package.assign(Start,I - Start);
   
   // Skip white space to the '('
   for (;I != Stop && isspace(*I) != 0 ; I++);
   
   // Parse a version
   if (I != Stop && *I == '(')
   {
      // Skip the '('
      for (I++; I != Stop && isspace(*I) != 0 ; I++);
      if (I + 3 >= Stop)
	 return 0;
      I = ConvertRelation(I,Op);
      
      // Skip whitespace
      for (;I != Stop && isspace(*I) != 0; I++);
      Start = I;
      for (;I != Stop && *I != ')'; I++);
      if (I == Stop || Start == I)
	 return 0;     
      
      // Skip trailing whitespace
      const char *End = I;
      for (; End > Start && isspace(End[-1]); End--);
      
      Ver.assign(Start,End-Start);
      I++;
   }
   else
   {
      Ver.clear();
      Op = pkgCache::Dep::NoOp;
   }
   
   // Skip whitespace
   for (;I != Stop && isspace(*I) != 0; I++);

   if (ParseArchFlags == true)
   {
      string arch = _config->Find("APT::Architecture");

      // Parse an architecture
      if (I != Stop && *I == '[')
      {
	 // malformed
         I++;
         if (I == Stop)
	    return 0; 
	 
         const char *End = I;
         bool Found = false;
      	 bool NegArch = false;
         while (I != Stop) 
	 {
            // look for whitespace or ending ']'
	    while (End != Stop && !isspace(*End) && *End != ']') 
	       End++;
	 
	    if (End == Stop) 
	       return 0;

	    if (*I == '!')
            {
	       NegArch = true;
	       I++;
            }

	    if (stringcmp(arch,I,End) == 0)
	       Found = true;
	    
	    if (*End++ == ']') {
	       I = End;
	       break;
	    }
	    
	    I = End;
	    for (;I != Stop && isspace(*I) != 0; I++);
         }

	 if (NegArch)
	    Found = !Found;
	 
         if (Found == false)
	    Package.clear(); /* not for this arch */
      }
      
      // Skip whitespace
      for (;I != Stop && isspace(*I) != 0; I++);
   }

   if (I != Stop && *I == '|')
      Op |= pkgCache::Dep::Or;
   
   if (I == Stop || *I == ',' || *I == '|')
   {
      if (I != Stop)
	 for (I++; I != Stop && isspace(*I) != 0; I++);
      return I;
   }
   
   return 0;
}
									/*}}}*/
// ListParser::ParseDepends - Parse a dependency list			/*{{{*/
// ---------------------------------------------------------------------
/* This is the higher level depends parser. It takes a tag and generates
   a complete depends tree for the given version. */
bool debListParser::ParseDepends(pkgCache::VerIterator Ver,
				 const char *Tag,unsigned int Type)
{
   const char *Start;
   const char *Stop;
   if (Section.Find(Tag,Start,Stop) == false)
      return true;
   
   srkString Package;
   srkString Version;
   unsigned int Op;

   while (1)
   {
      Start = ParseDepends(Start,Stop,Package,Version,Op);
      if (Start == 0) {
	 _error->Warning("Problem parsing dependency %s",Tag);
         return false;
      }
      
      if (NewDepends(Ver,Package,Version,Op,Type) == false)
	 return false;
      if (Start == Stop)
	 break;
   }
   return true;
}
									/*}}}*/
// ListParser::ParseProvides - Parse the provides list			/*{{{*/
// ---------------------------------------------------------------------
/* */
bool debListParser::ParseProvides(pkgCache::VerIterator Ver)
{
   const char *Start;
   const char *Stop;
   if (Section.Find("Provides",Start,Stop) == false)
      return true;
   
   srkString Package;
   srkString Version;
   unsigned int Op;

   while (1)
   {
      Start = ParseDepends(Start,Stop,Package,Version,Op);
      if (Start == 0) {
	 _error->Warning("Problem parsing Provides line");
         return false;
      }

      if (Op != pkgCache::Dep::NoOp) {
	 _error->Warning("Ignoring Provides line with DepCompareOp for package %s", std::string(Package).c_str());
      } else {
	 if (NewProvides(Ver,Package,Version) == false)
	    return false;
      }

      if (Start == Stop)
	 break;
   }
   
   return true;
}
									/*}}}*/
// ListParser::ParseTag - Parse the tag list				/*{{{*/
// ---------------------------------------------------------------------
/* */
bool debListParser::ParseTag(pkgCache::PkgIterator Pkg)
{
   const char *Start;
   const char *Stop;
   if (Section.Find("Tag",Start,Stop) == false)
      return true;
   
   while (1) {
      while (1) {
         if (Start == Stop)
            return true;
         if (Stop[-1] != ' ' && Stop[-1] != '\t')
            break;
         --Stop;
      }

      const char *Begin = Stop - 1;
      while (Begin != Start && Begin[-1] != ' ' && Begin[-1] != ',')
         --Begin;

      if (NewTag(Pkg, Begin, Stop - Begin) == false)
         return false;

      while (1) {
         if (Begin == Start)
            return true;
         if (Begin[-1] == ',')
            break;
         --Begin;
      }

      Stop = Begin - 1;
   }

   return true;
}
									/*}}}*/
// ListParser::GrabWord - Matches a word and returns			/*{{{*/
// ---------------------------------------------------------------------
/* Looks for a word in a list of words - for ParseStatus */
bool debListParser::GrabWord(string Word,WordList *List,unsigned char &Out)
{
   return GrabWord(srkString(Word), List, Out);
}

bool debListParser::GrabWord(const srkString &Word,WordList *List,unsigned char &Out)
{
   for (unsigned int C = 0; List[C].Str != 0; C++)
   {
      if (strncasecmp(Word.Start,List[C].Str,Word.Size) == 0)
      {
	 Out = List[C].Val;
	 return true;
      }
   }
   return false;
}
									/*}}}*/
// ListParser::Step - Move to the next section in the file		/*{{{*/
// ---------------------------------------------------------------------
/* This has to be carefull to only process the correct architecture */
bool debListParser::Step()
{
   iOffset = Tags.Offset();
   while (Tags.Step(Section) == true)
   {      
      const char *Start;
      const char *Stop;

      if (Section.Find("Package",Start,Stop) == false) {
         _error->Warning("Encountered a section with no Package: header");
	 continue;
      }

      /* See if this is the correct Architecture, if it isn't then we
         drop the whole section. A missing arch tag only happens (in theory)
         inside the Status file, so that is a positive return */

      if (Section.Find("Architecture",Start,Stop) == false)
	 return true;

      if (stringcmp(Arch,Start,Stop) == 0)
	 return true;

      if (stringcmp(Start,Stop,"all") == 0)
	 return true;

      if (stringcmp(Start,Stop,"cydia") == 0)
	 return true;

      iOffset = Tags.Offset();
   }   
   return false;
}
									/*}}}*/
// ListParser::LoadReleaseInfo - Load the release information		/*{{{*/
// ---------------------------------------------------------------------
/* */
bool debListParser::LoadReleaseInfo(pkgCache::PkgFileIterator FileI,
				    FileFd &File, string component)
{
   pkgTagFile Tags(&File, File.Size() + 256); // XXX
   pkgTagSection Section;
   if (Tags.Step(Section) == false)
      return false;

   //mvo: I don't think we need to fill that in (it's unused since apt-0.6)
   //FileI->Architecture = WriteUniqString(Arch);
   
   // apt-secure does no longer download individual (per-section) Release
   // file. to provide Component pinning we use the section name now
   FileI->Component = WriteUniqString(component);

   const char *Start;
   const char *Stop;
   if (Section.Find("Suite",Start,Stop) == true)
      FileI->Archive = WriteUniqString(Start,Stop - Start);
   if (Section.Find("Component",Start,Stop) == true)
      FileI->Component = WriteUniqString(Start,Stop - Start);
   if (Section.Find("Version",Start,Stop) == true)
      FileI->Version = WriteUniqString(Start,Stop - Start);
   if (Section.Find("Origin",Start,Stop) == true)
      FileI->Origin = WriteUniqString(Start,Stop - Start);
   if (Section.Find("Label",Start,Stop) == true)
      FileI->Label = WriteUniqString(Start,Stop - Start);
   if (Section.Find("Architecture",Start,Stop) == true)
      FileI->Architecture = WriteUniqString(Start,Stop - Start);
   
   if (Section.FindFlag("NotAutomatic",FileI->Flags,
			pkgCache::Flag::NotAutomatic) == false)
      _error->Warning("Bad NotAutomatic flag");

   return !_error->PendingError();
}
									/*}}}*/
// ListParser::GetPrio - Convert the priority from a string		/*{{{*/
// ---------------------------------------------------------------------
/* */
unsigned char debListParser::GetPrio(string Str)
{
   unsigned char Out;
   if (GrabWord(Str,PrioList,Out) == false)
      Out = pkgCache::State::Extra;
   
   return Out;
}
									/*}}}*/
