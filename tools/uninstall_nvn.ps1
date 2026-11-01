<# Remove this checkout's generated installation, retaining source for reinstall.
   No external input path is deleted. Shared system prerequisites are retained.
#>
param(
    [switch]$DryRun,
    [switch]$Yes,
    [switch]$RemovePreparedData,
    [switch]$RemoveUserData,
    [switch]$RemoveFirewallRule
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
. "$PSScriptRoot\nvn_common.ps1"

function Assert-PlainTree([string]$Path) {
    # Inspect ancestors and descendants before recursing: never follow junctions.
    $ancestor = $Path
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing linked path: $ancestor"
            }
        }
        $ancestor = Split-Path $ancestor -Parent
    }
    if (!(Test-Path -LiteralPath $Path)) { return }
    $item = Get-Item -LiteralPath $Path -Force
    if (!$item.PSIsContainer) { return }
    $pending = New-Object 'Collections.Generic.Stack[IO.DirectoryInfo]'
    $pending.Push([IO.DirectoryInfo]$item)
    while ($pending.Count) {
        $directory = $pending.Pop()
        foreach ($child in $directory.GetFileSystemInfos()) {
            if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing linked path: $($child.FullName)" }
            if ($child.Attributes -band [IO.FileAttributes]::Directory) { $pending.Push([IO.DirectoryInfo]$child) }
        }
    }
}

function Test-Inside([string]$Child, [string]$Parent) {
    return $Child.Equals($Parent, [StringComparison]::OrdinalIgnoreCase) -or
        $Child.StartsWith($Parent.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)
}

try {
    if (!(Test-Path -LiteralPath (Join-Path $root 'Install WindowSmash NVN.bat')) -or
        !(Test-Path -LiteralPath (Join-Path $root 'tools\build_nvn.py'))) {
        throw 'Project markers missing. Keep this script in the WindowSmash NVN tools folder.'
    }
    Assert-PlainTree $PSScriptRoot
    foreach ($process in @(Get-Process ssbu-nvn-vulkan -ErrorAction SilentlyContinue)) {
        if (!$process.Path -or (Test-Inside $process.Path $root)) { throw 'Close the NVN game before uninstalling.' }
    }

    Write-Host 'WindowSmash NVN uninstall'
    Write-Host "Project: $root"
    Write-Host 'Generated build, local dependencies and caches go to the Recycle Bin.'
    Write-Host 'Source and shared Python/Visual C++ installations are retained.'
    if (!$Yes -and !$DryRun) {
        if (!$RemovePreparedData) { $RemovePreparedData = (Read-Host 'Also remove prepared game data in dump? Reinstall will prepare it again [y/N]') -match '^y(es)?$' }
        if (!$RemoveUserData) { $RemoveUserData = (Read-Host 'Also remove saves, personal settings, screenshots and reports [y/N]') -match '^y(es)?$' }
    }
    $names = @('build', 'deps', 'cache', 'install-nvn.log')
    if ($RemovePreparedData) { $names += @('dump', 'version.txt', 'module_bases.txt', 'last_version.txt') }
    if ($RemoveUserData) {
        $names += @('save', 'save_backups', 'screenshots', 'reports', 'captures',
            'controllers.ini', 'display.ini', 'launcher.ini', 'online_name.txt',
            'netplay_id.txt', 'netplay_address.txt', 'online_direct.txt')
    }
    # Saved installer inputs are protected even if someone placed them in a
    # generated directory. Refuse the plan, rather than deleting source inputs.
    $owned = @()
    $folders = Join-Path $root 'dump\build_folders.txt'
    if (Test-Path -LiteralPath $folders) {
        foreach ($line in Get-Content -LiteralPath $folders) {
            if ($line -match '^(game|keys|firmware)=(.+)$') {
                $value = $Matches[2].Trim()
                if (![IO.Path]::IsPathRooted($value)) { $value = Join-Path $root $value }
                $owned += [IO.Path]::GetFullPath($value)
            }
        }
    }
    $targets = @()
    foreach ($name in $names) {
        $path = [IO.Path]::GetFullPath((Join-Path $root $name))
        if (!(Test-Inside $path $root) -or $path -eq $root) { throw "Unsafe target: $path" }
        foreach ($inputPath in $owned) {
            if ((Test-Inside $inputPath $path) -or (Test-Inside $path $inputPath)) {
                throw "Target overlaps an original input folder. Move your owned inputs and rerun setup first: $path"
            }
        }
        Write-Host "Checking: $path"
        Assert-PlainTree $path
        if (Test-Path -LiteralPath $path) { $targets += $path }
    }

    $shortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Play WindowSmash NVN.lnk'
    $removeShortcut = $false
    if (Test-Path -LiteralPath $shortcut) {
        Assert-PlainTree $shortcut
        $shell = New-Object -ComObject WScript.Shell
        $link = $shell.CreateShortcut($shortcut)
        $removeShortcut = Test-OwnedNvnShortcutTarget $link.TargetPath $root
    }
    foreach ($target in $targets) { Write-Host "Recycle: $target" }
    if ($removeShortcut) { Write-Host "Remove matching shortcut: $shortcut" }
    if (!$RemovePreparedData) { Write-Host 'Keeping prepared game data (dump).' }
    if (!$RemoveUserData) { Write-Host 'Keeping saves and personal settings/images.' }
    Write-Host 'Your original game/keys/firmware paths are not uninstall targets.'
    if ($DryRun) { Write-Host 'Dry run: no files, shortcuts or firewall rules changed.'; exit 0 }
    if (!$Yes -and (Read-Host 'Type UNINSTALL to confirm this plan') -cne 'UNINSTALL') {
        Write-Host 'Cancelled. No changes made.'; exit 0
    }

    Add-Type -AssemblyName Microsoft.VisualBasic
    foreach ($target in $targets) {
        # Recheck immediately before each action. Only native Windows file APIs.
        Assert-PlainTree $target
        if ((Get-Item -LiteralPath $target -Force).PSIsContainer) {
            [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($target, 'OnlyErrorDialogs', 'SendToRecycleBin', 'ThrowException')
        } else {
            [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($target, 'OnlyErrorDialogs', 'SendToRecycleBin', 'ThrowException')
        }
    }
    if ($removeShortcut) {
        $link = $shell.CreateShortcut($shortcut)
        if (Test-OwnedNvnShortcutTarget $link.TargetPath $root) { Remove-Item -LiteralPath $shortcut }
    }
    # Never remove rules by display name alone: another checkout may share it.
    if ($RemoveFirewallRule) {
        try {
            $exe = Join-Path $root 'build\nvn-opensmash-port\ssbu-nvn-vulkan.exe'
            foreach ($rule in @(Get-NetFirewallRule -DisplayName 'WindowSmash NVN (ssbu-nvn-vulkan.exe)' -ErrorAction SilentlyContinue)) {
                $app = $rule | Get-NetFirewallApplicationFilter
                if ($app.Program -ieq $exe) { $rule | Remove-NetFirewallRule -ErrorAction Stop }
            }
        } catch { Write-Warning 'Firewall cleanup failed. Rerun with -Yes -RemoveFirewallRule from an administrator terminal if needed.' }
    } else { Write-Host 'Optional firewall cleanup: run this uninstaller with -RemoveFirewallRule from an administrator terminal.' }
    Write-Host 'Uninstall complete. Source remains for reinstall; personal data is retained unless selected.'
    Write-Host 'Empty the Windows Recycle Bin when ready to reclaim disk space.'
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
