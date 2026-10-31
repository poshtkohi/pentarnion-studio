// Uninstall deletes files only inside the verified installation root.
// Documents, old external caches, recovery backups and credential stores stay.
var
  OwnedInstallRootForCleanup: String;

function SameOrParentPath(const Parent, Child: String): Boolean;
var
  Prefix, Candidate: String;
begin
  Prefix := AddBackslash(ExpandFileName(Parent));
  Candidate := AddBackslash(ExpandFileName(Child));
  Result := CompareText(Copy(Candidate, 1, Length(Prefix)), Prefix) = 0;
end;

function NoReparseAncestors(const Path: String): Boolean;
var
  Candidate, Parent: String;
  Attributes: LongWord;
begin
  Result := False;
  Candidate := RemoveBackslashUnlessRoot(ExpandFileName(Path));
  while Candidate <> '' do begin
    Attributes := RuntimeGetFileAttributes(RuntimeFilePath(Candidate));
    if (Attributes <> $FFFFFFFF) and ((Attributes and $400) <> 0) then Exit;
    Parent := RemoveBackslashUnlessRoot(ExtractFileDir(Candidate));
    if CompareText(Parent, Candidate) = 0 then Break;
    Candidate := Parent;
  end;
  Result := True;
end;

function PermittedApplicationRoot(const Target: String): Boolean;
var
  Root: String;
  ProtectedRoots: TArrayOfString;
  I: Integer;
begin
  Root := RemoveBackslashUnlessRoot(ExpandFileName(Target));
  Result := False;
  if (Length(Root) <= Length(ExtractFileDrive(Root)) + 1) or
     (Copy(Root, 1, 4) = '\\?\') then Exit;
  if not NoReparseAncestors(Root) then Exit;
  SetArrayLength(ProtectedRoots, 14);
  ProtectedRoots[0] := ExpandConstant('{win}');
  ProtectedRoots[1] := ExpandConstant('{sys}');
  ProtectedRoots[2] := GetEnv('USERPROFILE');
  ProtectedRoots[3] := ExpandConstant('{userdocs}');
  ProtectedRoots[4] := ExpandConstant('{localappdata}');
  ProtectedRoots[5] := ExpandConstant('{userappdata}');
  ProtectedRoots[6] := ExpandConstant('{userdesktop}');
  ProtectedRoots[7] := ExpandConstant('{commonpf}');
  ProtectedRoots[8] := ExpandConstant('{userprograms}');
  ProtectedRoots[9] := ExpandConstant('{localappdata}\Programs');
  ProtectedRoots[10] := ExpandConstant('{userdocs}\Pentarnion Studio');
  ProtectedRoots[11] := ExpandConstant('{userappdata}\Pentarnion');
  ProtectedRoots[12] := ExpandConstant('{localappdata}\Pentarnion Studio');
  ProtectedRoots[13] := ExpandConstant('{commonappdata}');
  for I := 0 to GetArrayLength(ProtectedRoots) - 1 do
    if SameOrParentPath(Root, ProtectedRoots[I]) then Exit;
  if DirExists(RuntimeFilePath(Root)) and
     ((RuntimeGetFileAttributes(RuntimeFilePath(Root)) and $400) <> 0) then Exit;
  Result := True;
end;

procedure UnlinkOwnedReparseEntriesBeforeLoggedRemoval(const Root: String);
var
  Directories, Links: TStringList;
  Index, I: Integer;
  Directory, Child: String;
  Attributes: LongWord;
  FindRec: TFindRec;
begin
  // Inno's logged payload removal runs before usPostUninstall. Remove links
  // themselves first so a replaced logged directory cannot redirect it.
  Directories := TStringList.Create;
  Links := TStringList.Create;
  try
    Directories.Add(Root);
    Index := 0;
    while Index < Directories.Count do begin
      Directory := Directories[Index];
      Index := Index + 1;
      if not SameOrParentPath(Root, Directory) or not NoReparseAncestors(Directory) then
        RaiseException('An installation path changed during uninstall validation. No linked target was accessed.');
      if FindFirst(RuntimeFilePath(AddBackslash(Directory) + '*'), FindRec) then begin
        try
          repeat
            if (FindRec.Name = '.') or (FindRec.Name = '..') then Continue;
            Child := AddBackslash(Directory) + FindRec.Name;
            if not SameOrParentPath(Root, Child) then
              RaiseException('An installation entry escaped its owned root.');
            if (FindRec.Attributes and $400) <> 0 then Links.Add(Child)
            else if (FindRec.Attributes and $10) <> 0 then Directories.Add(Child);
          until not FindNext(FindRec);
        finally
          FindClose(FindRec);
        end;
      end;
    end;
    for I := 0 to Links.Count - 1 do begin
      Child := Links[I];
      if not SameOrParentPath(Root, Child) or not NoReparseAncestors(ExtractFileDir(Child)) then
        RaiseException('An installation link parent changed during uninstall validation.');
      Attributes := RuntimeGetFileAttributes(RuntimeFilePath(Child));
      if (Attributes = $FFFFFFFF) then Continue;
      if (Attributes and $400) = 0 then
        RaiseException('An installation link changed during uninstall validation.');
      if (Attributes and $10) <> 0 then begin
        if not RemoveDir(RuntimeFilePath(Child)) then
          RaiseException('Cannot remove an installation directory link. Uninstall stopped before logged-file removal.');
      end else if not DeleteFile(RuntimeFilePath(Child)) then
        RaiseException('Cannot remove an installation file link. Uninstall stopped before logged-file removal.');
      Log('Removed an installation link without following its target: ' + Child);
    end;
  finally
    Links.Free;
    Directories.Free;
  end;
end;

function StudioSettingsKey(Param: String): String;
var
  Tag: String;
  I: Integer;
begin
  Tag := ExpandConstant('{param:TESTID|}');
  if Tag = '' then begin
    Result := 'Software\Pentarnion\Installer';
    Exit;
  end;
  if Length(Tag) > 80 then RaiseException('Invalid isolated settings test identifier.');
  for I := 1 to Length(Tag) do
    if Pos(Tag[I], 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-') = 0 then
      RaiseException('Invalid isolated settings test identifier.');
  Result := 'Software\Pentarnion\InstallerTests\' + Tag + '\Settings';
end;

procedure WriteInstallRootOwner;
var
  Root, Marker, Text: String;
begin
  Root := RemoveBackslashUnlessRoot(ExpandFileName(ExpandConstant('{app}')));
  if not PermittedApplicationRoot(Root) then
    RaiseException('The installation root cannot be safely owned by Studio.');
  Marker := AddBackslash(Root) + '.pentarnion-install-owner';
  Text := 'PentarnionStudioManagedInstall-v1' + #13#10 +
    'D5143A9E-9981-4E20-A5D6-38DD0D19836E' + #13#10 + Root + #13#10;
  if not SaveStringToFile(RuntimeFilePath(Marker), UTF8Encode(Text), False) then
    RaiseException('Cannot record the managed Studio installation root.');
end;

function AllOwnedShortcutPathsSafe(const Root: String): Boolean;
var Bytes: AnsiString;Paths: TStringList;I: Integer;
begin
  Result:=False;
  if not LoadStringFromFile(RuntimeFilePath(AddBackslash(Root)+'installer-shortcut-paths.txt'),Bytes) then Exit;
  Paths:=TStringList.Create;
  try
    Paths.Text:=UTF8Decode(Bytes);
    if (Paths.Count<2) or (Paths.Count>4096) or (Paths[0]<>'PentarnionShortcutPaths-v1') then Exit;
    for I:=1 to Paths.Count-1 do
      if (Trim(Paths[I])='') or not NoReparseAncestors(Paths[I]) then Exit;
    Result:=True;
  finally Paths.Free;end;
end;

function InitializeUninstall: Boolean;
var
  Root: String;
  Bytes: AnsiString;
  Lines: TStringList;
begin
  Result := False;
  Root := RemoveBackslashUnlessRoot(ExpandFileName(ExpandConstant('{app}')));
  if not PermittedApplicationRoot(Root) then begin
    SuppressibleMsgBox('The installation location is unsafe for automatic cleanup. No user files were removed.', mbError, MB_OK, IDOK);
    Exit;
  end;
  if not LoadStringFromFile(RuntimeFilePath(AddBackslash(Root) + '.pentarnion-install-owner'), Bytes) then begin
    SuppressibleMsgBox('The Studio installation ownership record is missing. Automatic cleanup was stopped.', mbError, MB_OK, IDOK);
    Exit;
  end;
  Lines := TStringList.Create;
  try
    Lines.Text := UTF8Decode(Bytes);
    if (Lines.Count < 3) or (Lines[0] <> 'PentarnionStudioManagedInstall-v1') or
       (Lines[1] <> 'D5143A9E-9981-4E20-A5D6-38DD0D19836E') or
       (CompareText(RemoveBackslashUnlessRoot(ExpandFileName(Lines[2])), Root) <> 0) then begin
      SuppressibleMsgBox('The Studio installation ownership record does not match this folder. Cleanup was stopped.', mbError, MB_OK, IDOK);
      Exit;
    end;
  finally
    Lines.Free;
  end;
  // Shortcut files are logged outside {app}. Refuse a replaced shortcut parent
  // before Inno can follow it to an unrelated file with the same name.
  if not AllOwnedShortcutPathsSafe(Root) then begin
    SuppressibleMsgBox('An owned shortcut path (including earlier update locations) is linked or its ownership record is unavailable. Uninstall stopped before logged-file removal.', mbError, MB_OK, IDOK);
    Exit;
  end;
  OwnedInstallRootForCleanup := Root;
  UnlinkOwnedReparseEntriesBeforeLoggedRemoval(Root);
  Result := True;
end;

procedure AssertInsideOwnedInstall(const Path: String);
begin
  if (OwnedInstallRootForCleanup = '') or
     not SameOrParentPath(OwnedInstallRootForCleanup, Path) then
    RaiseException('Uninstall cleanup escaped its verified installation root.');
end;

procedure RemoveOwnedApplicationTree;
var
  Directories: TStringList;
  Index, I: Integer;
  Directory, Child: String;
  Attributes: LongWord;
  FindRec: TFindRec;
begin
  if OwnedInstallRootForCleanup = '' then
    RaiseException('The Studio uninstall root was not verified.');
  if not DirExists(RuntimeFilePath(OwnedInstallRootForCleanup)) then Exit;
  Directories := TStringList.Create;
  try
    Directories.Add(OwnedInstallRootForCleanup);
    Index := 0;
    while Index < Directories.Count do begin
      Directory := Directories[Index];
      Index := Index + 1;
      AssertInsideOwnedInstall(Directory);
      Attributes := RuntimeGetFileAttributes(RuntimeFilePath(Directory));
      if (Attributes and $400) <> 0 then begin
        // Remove a link itself; never enumerate or follow its target.
        if not RemoveDir(RuntimeFilePath(Directory)) then
          RaiseException('Cannot remove an installation directory link.');
        Continue;
      end;
      if FindFirst(RuntimeFilePath(AddBackslash(Directory) + '*'), FindRec) then begin
        try
          repeat
            if (FindRec.Name = '.') or (FindRec.Name = '..') then Continue;
            Child := AddBackslash(Directory) + FindRec.Name;
            AssertInsideOwnedInstall(Child);
            if (FindRec.Attributes and $10) <> 0 then begin
              if (FindRec.Attributes and $400) <> 0 then begin
                if not RemoveDir(RuntimeFilePath(Child)) then
                  RaiseException('Cannot remove an installation directory link.');
              end else Directories.Add(Child);
            end else if not DeleteFile(RuntimeFilePath(Child)) then
              RaiseException('An installation file is still in use. Close Studio and its open installation documents, then retry uninstall.');
          until not FindNext(FindRec);
        finally
          FindClose(FindRec);
        end;
      end;
    end;
    for I := Directories.Count - 1 downto 0 do begin
      Directory := Directories[I];
      AssertInsideOwnedInstall(Directory);
      if DirExists(RuntimeFilePath(Directory)) and not RemoveDir(RuntimeFilePath(Directory)) then
        RaiseException('The installation folder could not be completely removed. Close files using it and retry uninstall.');
    end;
    Log('Removed the verified Studio installation folder only: ' + OwnedInstallRootForCleanup);
  finally
    Directories.Free;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then RemoveOwnedApplicationTree;
end;


