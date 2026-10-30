[Setup]
AppName=Console Strip
AppVersion=1.0.0
AppPublisher=DaniArieta
DefaultDirName={autopf}\Console Strip
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=output
OutputBaseFilename=ConsoleStrip-Setup
Compression=lzma
SolidCompression=yes

[Files]
Source: "dist\Console Strip.vst3\*"; DestDir: "{commonpf64}\Common Files\VST3\Console Strip.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
