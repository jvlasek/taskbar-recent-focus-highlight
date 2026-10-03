# Read-only screen geometry helper. No UIA Invoke/SetFocus calls.
param([int]$ExplorerPid, [switch]$AtPoint, [int]$X, [int]$Y)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName WindowsBase
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DpiContext {
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
}
'@
[void][DpiContext]::SetThreadDpiAwarenessContext([IntPtr](-4))
try {
    if ($AtPoint) {
        $element = [System.Windows.Automation.AutomationElement]::FromPoint((New-Object System.Windows.Point($X,$Y)))
        $hitRoot = $null
        while ($element) {
            $c = $element.Current
            if ($c.ProcessId -eq $ExplorerPid -and $c.ClassName -in @('Shell_TrayWnd','Shell_SecondaryTrayWnd','XamlExplorerHostIslandWindow','TaskListThumbnailWnd')) { $hitRoot = $element }
            if ($c.ProcessId -eq $ExplorerPid -and $c.ClassName -match 'TaskListButton|TaskItemThumbnail') {
                $r = $c.BoundingRectangle
                ConvertTo-Json -InputObject @{name=$c.Name;id=$c.AutomationId;class=$c.ClassName;rect=@($r.Left,$r.Top,$r.Right,$r.Bottom)} -Compress
                exit 0
            }
            $element = [System.Windows.Automation.TreeWalker]::RawViewWalker.GetParent($element)
        }
        # Some taskbar providers return only the HWND root from FromPoint.
        # Require a unique live descendant containing the point under that hit root.
        $hits = @()
        if ($hitRoot) {
            foreach ($child in $hitRoot.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
                $c = $child.Current; $r = $c.BoundingRectangle
                if (-not $c.IsOffscreen -and $c.ClassName -match 'TaskListButton|TaskItemThumbnail' -and $r.Contains($X,$Y)) {
                    $hits += @{name=$c.Name;id=$c.AutomationId;class=$c.ClassName;rect=@($r.Left,$r.Top,$r.Right,$r.Bottom)}
                }
            }
        }
        if ($hits.Count -eq 1) { ConvertTo-Json -InputObject $hits[0] -Compress; exit 0 }
        'null'
        exit 0
    }
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $condition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $ExplorerPid)
    $windows = $root.FindAll([System.Windows.Automation.TreeScope]::Children, $condition)
    $rows = @()
    foreach ($window in $windows) {
        $class = $window.Current.ClassName
        if ($class -notin @('Shell_TrayWnd','Shell_SecondaryTrayWnd','XamlExplorerHostIslandWindow','TaskListThumbnailWnd')) { continue }
        $elements = $window.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
        foreach ($element in $elements) {
            $c = $element.Current
            if ($c.IsOffscreen -or $c.BoundingRectangle.IsEmpty) { continue }
            if ($c.ClassName -notmatch 'TaskListButton|TaskItemThumbnail') { continue }
            $r = $c.BoundingRectangle
            if ($r.Width -le 0 -or $r.Height -le 0) { continue }
            $rows += @{name=$c.Name; id=$c.AutomationId; class=$c.ClassName;
                root=[long]$window.Current.NativeWindowHandle;
                rect=@($r.Left,$r.Top,$r.Right,$r.Bottom)}
        }
    }
    ConvertTo-Json -InputObject @($rows) -Depth 4 -Compress
} catch { [Console]::Error.WriteLine($_); exit 1 }
