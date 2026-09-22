# PowerShell 快速提取传奇客户端素材
param (
    [Parameter(Mandatory=$true)]
    [string]$ClientDir,
    [string]$OutDir = "templates/game/legend/assets"
)

Write-Host ">>> 开始提取传奇原版打击感与大爆素材..." -ForegroundColor Green
python scripts/extract_legend_assets.py --client $ClientDir --out $OutDir
Write-Host ">>> 提取完成！" -ForegroundColor Green
