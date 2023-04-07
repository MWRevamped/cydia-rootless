// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: tagfile.cc,v 1.37.2.2 2003/12/31 16:02:30 mdz Exp $
/* ######################################################################

   Fast scanner for RFC-822 type header information
   
   This uses a rotating buffer to load the package information into.
   The scanner runs over it and isolates and indexes a single section.
   
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <apt-pkg/tagfile.h>
#include <apt-pkg/error.h>
#include <apt-pkg/strutl.h>

#include <apti18n.h>
    
#include <string>
#include <stdio.h>
#include <ctype.h>
									/*}}}*/

using std::string;

// TagFile::pkgTagFile - Constructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgTagFile::pkgTagFile(FileFd *pFd,unsigned long Size) :
     Fd(*pFd)
{
   if (Fd.IsOpen() == false || Fd.Size() == 0)
   {
      _error->Discard();
      Map = NULL;
      Buffer = 0;
      Start = End = Buffer = 0;
      Done = true;
      iOffset = 0;
      return;
   }
   
   Map = new MMap(*pFd, MMap::ReadOnly);
   Buffer = reinterpret_cast<char *>(Map->Data());
   Start = End = Buffer;
   Done = false;
   iOffset = 0;
   Fill();
}
									/*}}}*/
// TagFile::~pkgTagFile - Destructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgTagFile::~pkgTagFile()
{
   delete Map;
}
									/*}}}*/
// TagFile::Step - Advance to the next section				/*{{{*/
// ---------------------------------------------------------------------
/* If the Section Scanner fails we refill the buffer and try again. 
 * If that fails too, double the buffer size and try again until a
 * maximum buffer is reached.
 */
bool pkgTagFile::Step(pkgTagSection &Tag)
{
   if (Tag.Scan(Start,End - Start) == false)
   {
      if (Start == End)
         return false;
      else
	 return _error->Error(_("Unable to parse package file %s (1)"),
				 Fd.Name().c_str());
   }
   Start += Tag.size();
   iOffset += Tag.size();

   Tag.Trim();
   return true;
}
									/*}}}*/
// TagFile::Fill - Top up the buffer					/*{{{*/
// ---------------------------------------------------------------------
/* This takes the bit at the end of the buffer and puts it at the start
   then fills the rest from the file */
bool pkgTagFile::Fill()
{
   unsigned int Size(Map->Size());
   End = Buffer + Size;
   if (iOffset >= Size)
      return false;
   Start = Buffer + iOffset;
   return true;
}
									/*}}}*/
// TagFile::Jump - Jump to a pre-recorded location in the file		/*{{{*/
// ---------------------------------------------------------------------
/* This jumps to a pre-recorded file location and reads the record
   that is there */
bool pkgTagFile::Jump(pkgTagSection &Tag,unsigned long Offset)
{
   // We are within a buffer space of the next hit..
   if (Offset >= iOffset && iOffset + (End - Start) > Offset)
   {
      unsigned long Dist = Offset - iOffset;
      Start += Dist;
      iOffset += Dist;
      return Step(Tag);
   }

   // Reposition and reload..
   iOffset = Offset;
   Done = false;
   End = Start = Buffer;
   
   if (Fill() == false)
      return false;

   if (Tag.Scan(Start,End - Start) == false)
      return _error->Error(_("Unable to parse package file %s (2)"),Fd.Name().c_str());
   
   return true;
}
									/*}}}*/
// TagSection::Scan - Scan for the end of the header information	/*{{{*/
// ---------------------------------------------------------------------
/* This looks for the first double new line in the data stream. It also
   indexes the tags in the section. This very simple hash function for the
   last 8 letters gives very good performance on the debian package files */
inline static unsigned long AlphaHash(const char *Text, const char *End = 0)
{
   unsigned long Res = 0;
   for (; Text != End && *Text != ':' && *Text != 0; Text++)
      Res = ((unsigned long)(*Text) & 0xDF) ^ (Res << 1);
   return Res & 0xFF;
}

bool pkgTagSection::Scan(const char *Start,unsigned long MaxLength)
{
   const char *End = Start + MaxLength;
   Stop = Section = Start;
   memset(AlphaIndexes,0,sizeof(AlphaIndexes));

   if (Stop == 0)
      return false;

   TagCount = 0;
   while (TagCount+1 < sizeof(Indexes)/sizeof(Indexes[0]) && Stop < End)
   {
       TrimRecord(true,End);

      // Start a new index and add it to the hash
      if (isspace(Stop[0]) == 0)
      {
	 Indexes[TagCount++] = Stop - Section;
         unsigned long hash(AlphaHash(Stop, End));
         while (AlphaIndexes[hash] != 0)
            hash = (hash + 1) % (sizeof(AlphaIndexes) / sizeof(AlphaIndexes[0]));
	 AlphaIndexes[hash] = TagCount;
      }

      Stop = (const char *)memchr(Stop,'\n',End - Stop);
      
      if (Stop == 0) {
         Stop = End;
         goto end;
      }

      for (; Stop+1 < End && Stop[1] == '\r'; Stop++);

      // Double newline marks the end of the record
      if (Stop+1 == End || Stop[1] == '\n')
      end: {
	 Indexes[TagCount] = Stop - Section;
	 TrimRecord(false,End);
	 return true;
      }
      
      Stop++;
   }

   return false;
}
									/*}}}*/
// TagSection::TrimRecord - Trim off any garbage before/after a record	/*{{{*/
// ---------------------------------------------------------------------
/* There should be exactly 2 newline at the end of the record, no more. */
void pkgTagSection::TrimRecord(bool BeforeRecord, const char*& End)
{
   if (BeforeRecord == true)
      return;
   for (; Stop < End && (Stop[0] == '\n' || Stop[0] == '\r'); Stop++);
}
									/*}}}*/
// TagSection::Trim - Trim off any trailing garbage			/*{{{*/
// ---------------------------------------------------------------------
/* There should be exactly 1 newline at the end of the buffer, no more. */
void pkgTagSection::Trim()
{
   for (; Stop > Section + 2 && (Stop[-2] == '\n' || Stop[-2] == '\r'); Stop--);
}
									/*}}}*/
// TagSection::Find - Locate a tag					/*{{{*/
// ---------------------------------------------------------------------
/* This searches the section for a tag that matches the given string. */
bool pkgTagSection::Find(const char *Tag,unsigned &Pos) const
{
   unsigned int Length = strlen(Tag);
   unsigned int J = AlphaHash(Tag);
   
   for (unsigned int Counter = 0; Counter != TagCount; Counter++,
	J = (J+1)%(sizeof(AlphaIndexes)/sizeof(AlphaIndexes[0])))
   {
      unsigned int I = AlphaIndexes[J];
      if (I == 0)
         return false;
      I--;

      const char *St;
      St = Section + Indexes[I];
      if (strncasecmp(Tag,St,Length) != 0)
	 continue;

      // Make sure the colon is in the right place
      const char *C = St + Length;
      for (; isspace(*C) != 0; C++);
      if (*C != ':')
	 continue;
      Pos = I;
      return true;
   }

   Pos = 0;
   return false;
}
									/*}}}*/
// TagSection::Find - Locate a tag					/*{{{*/
// ---------------------------------------------------------------------
/* This searches the section for a tag that matches the given string. */
bool pkgTagSection::Find(const char *Tag,const char *&Start,
		         const char *&End) const
{
   unsigned int Length = strlen(Tag);
   unsigned int J = AlphaHash(Tag);
   
   for (unsigned int Counter = 0; Counter != TagCount; Counter++,
	J = (J+1)%(sizeof(AlphaIndexes)/sizeof(AlphaIndexes[0])))
   {
      unsigned int I = AlphaIndexes[J];
      if (I == 0)
         return false;
      I--;

      const char *St;
      St = Section + Indexes[I];
      if (strncasecmp(Tag,St,Length) != 0)
	 continue;
      
      // Make sure the colon is in the right place
      const char *C = St + Length;
      for (; isspace(*C) != 0; C++);
      if (*C != ':')
	 continue;

      // Strip off the gunk from the start end
      Start = C;
      End = Section + Indexes[I+1];
      if (Start >= End)
	 return _error->Error("Internal parsing error");
      
      for (; (isspace(*Start) != 0 || *Start == ':') && Start < End; Start++);
      for (; isspace(End[-1]) != 0 && End > Start; End--);
      
      return true;
   }
   
   Start = End = 0;
   return false;
}
									/*}}}*/
// TagSection::FindS - Find a string					/*{{{*/
// ---------------------------------------------------------------------
/* */
string pkgTagSection::FindS(const char *Tag) const
{
   const char *Start;
   const char *End;
   if (Find(Tag,Start,End) == false)
      return string();
   return string(Start,End);      
}
									/*}}}*/
// TagSection::FindI - Find an integer					/*{{{*/
// ---------------------------------------------------------------------
/* */
signed int pkgTagSection::FindI(const char *Tag,signed long Default) const
{
   const char *Start;
   const char *Stop;
   if (Find(Tag,Start,Stop) == false)
      return Default;

   // Copy it into a temp buffer so we can use strtol
   char S[300];
   if ((unsigned)(Stop - Start) >= sizeof(S))
      return Default;
   strncpy(S,Start,Stop-Start);
   S[Stop - Start] = 0;
   
   char *End;
   signed long Result = strtol(S,&End,10);
   if (S == End)
      return Default;
   return Result;
}
									/*}}}*/
// TagSection::FindFlag - Locate a yes/no type flag			/*{{{*/
// ---------------------------------------------------------------------
/* The bits marked in Flag are masked on/off in Flags */
bool pkgTagSection::FindFlag(const char *Tag,unsigned long &Flags,
			     unsigned long Flag) const
{
   const char *Start;
   const char *Stop;
   if (Find(Tag,Start,Stop) == false)
      return true;
   
   switch (StringToBool(string(Start,Stop)))
   {     
      case 0:
      Flags &= ~Flag;
      return true;

      case 1:
      Flags |= Flag;
      return true;

      default:
      _error->Warning("Unknown flag value: %s",string(Start,Stop).c_str());
      return true;
   }
   return true;
}
									/*}}}*/
// TFRewrite - Rewrite a control record					/*{{{*/
// ---------------------------------------------------------------------
/* This writes the control record to stdout rewriting it as necessary. The
   override map item specificies the rewriting rules to follow. This also
   takes the time to sort the feild list. */

/* The order of this list is taken from dpkg source lib/parse.c the fieldinfos
   array. */
static const char *iTFRewritePackageOrder[] = {
                          "Package",
                          "Essential",
                          "Status",
                          "Priority",
                          "Section",
                          "Installed-Size",
                          "Maintainer",
                          "Architecture",
                          "Source",
                          "Version",
                           "Revision",         // Obsolete
                           "Config-Version",   // Obsolete
                          "Replaces",
                          "Provides",
                          "Depends",
                          "Pre-Depends",
                          "Recommends",
                          "Suggests",
                          "Conflicts",
                          "Breaks",
                          "Conffiles",
                          "Filename",
                          "Size",
                          "MD5Sum",
                          "SHA1",
                          "SHA256",
                           "MSDOS-Filename",   // Obsolete
                          "Description",
                          0};
static const char *iTFRewriteSourceOrder[] = {"Package",
                                      "Source",
                                      "Binary",
                                      "Version",
                                      "Priority",
                                      "Section",
                                      "Maintainer",
                                      "Build-Depends",
                                      "Build-Depends-Indep",
                                      "Build-Conflicts",
                                      "Build-Conflicts-Indep",
                                      "Architecture",
                                      "Standards-Version",
                                      "Format",
                                      "Directory",
                                      "Files",
                                      0};   

/* Two levels of initialization are used because gcc will set the symbol
   size of an array to the length of the array, causing dynamic relinking 
   errors. Doing this makes the symbol size constant */
const char **TFRewritePackageOrder = iTFRewritePackageOrder;
const char **TFRewriteSourceOrder = iTFRewriteSourceOrder;
   
bool TFRewrite(FILE *Output,pkgTagSection const &Tags,const char *Order[],
	       TFRewriteData *Rewrite)
{
   unsigned char Visited[256];   // Bit 1 is Order, Bit 2 is Rewrite
   for (unsigned I = 0; I != 256; I++)
      Visited[I] = 0;

   // Set new tag up as necessary.
   for (unsigned int J = 0; Rewrite != 0 && Rewrite[J].Tag != 0; J++)
   {
      if (Rewrite[J].NewTag == 0)
	 Rewrite[J].NewTag = Rewrite[J].Tag;
   }
   
   // Write all all of the tags, in order.
   for (unsigned int I = 0; Order[I] != 0; I++)
   {
      bool Rewritten = false;
      
      // See if this is a field that needs to be rewritten
      for (unsigned int J = 0; Rewrite != 0 && Rewrite[J].Tag != 0; J++)
      {
	 if (strcasecmp(Rewrite[J].Tag,Order[I]) == 0)
	 {
	    Visited[J] |= 2;
	    if (Rewrite[J].Rewrite != 0 && Rewrite[J].Rewrite[0] != 0)
	    {
	       if (isspace(Rewrite[J].Rewrite[0]))
		  fprintf(Output,"%s:%s\n",Rewrite[J].NewTag,Rewrite[J].Rewrite);
	       else
		  fprintf(Output,"%s: %s\n",Rewrite[J].NewTag,Rewrite[J].Rewrite);
	    }
	    
	    Rewritten = true;
	    break;
	 }
      }      
	    
      // See if it is in the fragment
      unsigned Pos;
      if (Tags.Find(Order[I],Pos) == false)
	 continue;
      Visited[Pos] |= 1;

      if (Rewritten == true)
	 continue;
      
      /* Write out this element, taking a moment to rewrite the tag
         in case of changes of case. */
      const char *Start;
      const char *Stop;
      Tags.Get(Start,Stop,Pos);
      
      if (fputs(Order[I],Output) < 0)
	 return _error->Errno("fputs","IO Error to output");
      Start += strlen(Order[I]);
      if (fwrite(Start,Stop - Start,1,Output) != 1)
	 return _error->Errno("fwrite","IO Error to output");
      if (Stop[-1] != '\n')
	 fprintf(Output,"\n");
   }   

   // Now write all the old tags that were missed.
   for (unsigned int I = 0; I != Tags.Count(); I++)
   {
      if ((Visited[I] & 1) == 1)
	 continue;

      const char *Start;
      const char *Stop;
      Tags.Get(Start,Stop,I);
      const char *End = Start;
      for (; End < Stop && *End != ':'; End++);

      // See if this is a field that needs to be rewritten
      bool Rewritten = false;
      for (unsigned int J = 0; Rewrite != 0 && Rewrite[J].Tag != 0; J++)
      {
	 if (stringcasecmp(Start,End,Rewrite[J].Tag) == 0)
	 {
	    Visited[J] |= 2;
	    if (Rewrite[J].Rewrite != 0 && Rewrite[J].Rewrite[0] != 0)
	    {
	       if (isspace(Rewrite[J].Rewrite[0]))
		  fprintf(Output,"%s:%s\n",Rewrite[J].NewTag,Rewrite[J].Rewrite);
	       else
		  fprintf(Output,"%s: %s\n",Rewrite[J].NewTag,Rewrite[J].Rewrite);
	    }
	    
	    Rewritten = true;
	    break;
	 }
      }      
      
      if (Rewritten == true)
	 continue;
      
      // Write out this element
      if (fwrite(Start,Stop - Start,1,Output) != 1)
	 return _error->Errno("fwrite","IO Error to output");
      if (Stop[-1] != '\n')
	 fprintf(Output,"\n");
   }
   
   // Now write all the rewrites that were missed
   for (unsigned int J = 0; Rewrite != 0 && Rewrite[J].Tag != 0; J++)
   {
      if ((Visited[J] & 2) == 2)
	 continue;
      
      if (Rewrite[J].Rewrite != 0 && Rewrite[J].Rewrite[0] != 0)
      {
	 if (isspace(Rewrite[J].Rewrite[0]))
	    fprintf(Output,"%s:%s\n",Rewrite[J].NewTag,Rewrite[J].Rewrite);
	 else
	    fprintf(Output,"%s: %s\n",Rewrite[J].NewTag,Rewrite[J].Rewrite);
      }      
   }
      
   return true;
}
									/*}}}*/
