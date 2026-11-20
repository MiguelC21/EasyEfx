; Script de Inno Setup para EasyEfx
#define MyAppName "EasyEfx"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Miguel Carreño"
#define MyAppURL "https://github.com/MiguelC21/EasyEfx"

[Setup]
AppId={{D37E64B2-9F8C-4C7E-B19A-87586A1D20E1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputBaseFilename=EasyEfx_Installer
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
OutputDir=..\dist

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Instalación completa (Plugin VST3 + Aplicación Standalone)"
Name: "vst3only"; Description: "Solo Plugin VST3 (para DAWs)"
Name: "custom"; Description: "Instalación personalizada"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Plugin VST3 (64-bit) para FL Studio, Ableton, Cubase, Reaper, etc."; Types: full vst3only custom; Flags: fixed
Name: "standalone"; Description: "Aplicación Standalone (.exe independiente para tocar sin DAW)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; Components: standalone

[Files]
; Plugin VST3 hacia la carpeta estándar de Windows: C:\Program Files\Common Files\VST3\EasyEfx.vst3
Source: "..\build\EasyEfx_artefacts\Release\VST3\EasyEfx.vst3\*"; DestDir: "{commoncf64}\VST3\EasyEfx.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3

; Aplicación Standalone hacia Program Files\EasyEfx
Source: "..\build\EasyEfx_artefacts\Release\Standalone\EasyEfx.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\EasyEfx.exe"; Components: standalone
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\EasyEfx.exe"; Tasks: desktopicon; Components: standalone

[Run]
Filename: "{app}\EasyEfx.exe"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent; Components: standalone
