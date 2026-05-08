Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile('resources\images\nirvanahq_favicon.png')

$sizes = @(
    @{ Size = 28; Name = 'menu_icon.png' },
    @{ Size = 48; Name = 'store_icon_48.png' },
    @{ Size = 144; Name = 'store_icon_144.png' }
)

foreach ($item in $sizes) {
    $s = $item.Size
    $name = $item.Name
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.DrawImage($img, 0, 0, $s, $s)
    $g.Dispose()
    $bmp.Save("resources\images\$name", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "Created $name (${s}x${s})"
}

$img.Dispose()
