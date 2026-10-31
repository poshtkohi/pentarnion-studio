// Verify all update paths and copy changed files to a hash-checked recovery run
// before Inno replaces the first payload file. Never terminate a running app.
procedure AssertTreeHasNoLinks(const Root: String);
var Directories: TStringList;Index: Integer;Current,Child: String;FindRec: TFindRec;
begin
  if not NoReparseAncestors(Root) then RaiseException('An install/workspace/shortcut ancestor is a junction or symbolic link.');
  if not DirExists(RuntimeFilePath(Root)) then Exit;
  Directories:=TStringList.Create;
  try
    Directories.Add(Root);Index:=0;
    while Index<Directories.Count do begin
      Current:=Directories[Index];Index:=Index+1;
      if not NoReparseAncestors(Current) then RaiseException('A directory changed to a link during validation.');
      if FindFirst(RuntimeFilePath(AddBackslash(Current)+'*'),FindRec) then begin
        try
          repeat
            if (FindRec.Name='.') or (FindRec.Name='..') then Continue;
            Child:=AddBackslash(Current)+FindRec.Name;
            if (FindRec.Attributes and $400)<>0 then RaiseException('Linked entries are not followed during installation or update.');
            if (FindRec.Attributes and $10)<>0 then Directories.Add(Child);
          until not FindNext(FindRec);
        finally FindClose(FindRec);end;
      end;
    end;
  finally Directories.Free;end;
end;

function OwnedApplicationDirectory(const Root: String): Boolean;
var Bytes: AnsiString;Lines: TStringList;
begin
  Result:=False;
  if not LoadStringFromFile(RuntimeFilePath(AddBackslash(Root)+'.pentarnion-install-owner'),Bytes) then Exit;
  Lines:=TStringList.Create;
  try
    Lines.Text:=UTF8Decode(Bytes);
    Result:=(Lines.Count>=3) and (Lines[0]='PentarnionStudioManagedInstall-v1') and
      (Lines[1]='D5143A9E-9981-4E20-A5D6-38DD0D19836E') and
      (CompareText(RemoveBackslashUnlessRoot(ExpandFileName(Lines[2])),RemoveBackslashUnlessRoot(ExpandFileName(Root)))=0);
  finally Lines.Free;end;
end;

function SafeApplicationDirectory(const Target: String): Boolean;
var FindRec: TFindRec;HasContent: Boolean;
begin
  Result:=False;if not PermittedApplicationRoot(Target) then Exit;
  HasContent:=False;
  if FindFirst(RuntimeFilePath(AddBackslash(Target)+'*'),FindRec) then begin
    try repeat if (FindRec.Name<>'.') and (FindRec.Name<>'..') then HasContent:=True;until not FindNext(FindRec);
    finally FindClose(FindRec);end;
  end;
  Result:=not HasContent or OwnedApplicationDirectory(Target);
end;

function RecoveryRun: String;
var Base,Candidate,Stamp: String;I: Integer;
begin
  Base:=ExpandConstant('{param:RECOVERYROOT|}');
  if Base='' then Base:=ExpandConstant('{localappdata}\Pentarnion Studio\InstallerRecovery');
  Base:=ExpandFileName(Base);
  if SameOrParentPath(ExpandConstant('{app}'),Base) then RaiseException('Installer recovery must remain outside the application folder.');
  if not NoReparseAncestors(Base) then RaiseException('Installer recovery has a linked ancestor.');
  if not ForceDirectories(RuntimeFilePath(Base)) then RaiseException('Cannot create installer recovery folder.');
  Stamp:=GetDateTimeString('yyyymmdd-hhnnss','-',':');
  for I:=0 to 1023 do begin
    Candidate:=AddBackslash(Base)+Stamp+'-'+IntToStr(I);
    if not DirExists(RuntimeFilePath(Candidate)) and CreateDir(RuntimeFilePath(Candidate)) then begin Result:=Candidate;Exit;end;
  end;
  RaiseException('Cannot allocate a unique installer recovery run.');
end;

procedure BackupReplacedApplicationFiles;
var Records: TArrayOfString;I,Separator: Integer;Relative,Expected,Original,Backup,Actual,Recovery,Manifest,Root: String;Handle: THandle;
begin
  Root:=ExpandConstant('{app}');
  if not OwnedApplicationDirectory(Root) then Exit;
  AssertTreeHasNoLinks(Root);
  ExtractTemporaryFile('incoming-file-hashes.txt');
  if not LoadStringsFromFile(ExpandConstant('{tmp}\incoming-file-hashes.txt'),Records) then RaiseException('Cannot read the incoming payload inventory.');
  Recovery:='';Manifest:='Pentarnion Studio verified pre-update recovery'+#13#10+'relative_path|saved_sha256'+#13#10;
  for I:=0 to GetArrayLength(Records)-1 do begin
    Separator:=Pos('|',Records[I]);if Separator<=1 then RaiseException('Invalid incoming payload inventory.');
    Relative:=Copy(Records[I],1,Separator-1);Expected:=Copy(Records[I],Separator+1,Length(Records[I]));
    if (Pos(':',Relative)>0) or (Relative[1]='\') or (Pos('\..\','\'+Relative+'\')>0) or (Pos('\.\','\'+Relative+'\')>0) or (Length(Expected)<>64) then RaiseException('Incoming payload inventory escaped the owned root.');
    Original:=AddBackslash(Root)+Relative;
    if not FileExists(RuntimeFilePath(Original)) then Continue;
    // Exclusive opening detects running executables, mapped DLLs and open
    // documentation. Refuse before replacing anything; never close the process.
    Handle:=RuntimeCreateFile(RuntimeFilePath(Original),$80000000,0,0,3,$80,0);
    if Handle=THandle(-1) then RaiseException('Close Studio, running calculations and documents from this installation before updating. No application file has been replaced.');
    RuntimeCloseHandle(Handle);
    Actual:=Lowercase(GetSHA256OfFile(RuntimeFilePath(Original)));
    if Actual=Lowercase(Expected) then Continue;
    if Recovery='' then Recovery:=RecoveryRun;
    Backup:=AddBackslash(Recovery)+Relative;
    if not ForceDirectories(RuntimeFilePath(ExtractFileDir(Backup))) or not CopyFile(RuntimeFilePath(Original),RuntimeFilePath(Backup),True) then RaiseException('Cannot back up the prior release; update stopped.');
    if Lowercase(GetSHA256OfFile(RuntimeFilePath(Backup)))<>Actual then RaiseException('Prior-release backup hash mismatch; update stopped.');
    Manifest:=Manifest+Relative+'|'+Actual+#13#10;
  end;
  if Recovery<>'' then begin
    if not SaveStringToFile(RuntimeFilePath(AddBackslash(Recovery)+'recovery-manifest.txt'),UTF8Encode(Manifest),False) then RaiseException('Cannot record verified backup; update stopped.');
    Log('Verified prior-release backup: '+Recovery);
  end;
end;
