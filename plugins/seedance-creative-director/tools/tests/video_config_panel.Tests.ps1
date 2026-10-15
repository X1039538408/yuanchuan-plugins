Describe 'Seedance video configuration panel controller contract' {
  It 'advertises the batch preparation action for both providers' {
    $dreamina = & pwsh -NoProfile -File (Join-Path $PSScriptRoot '..\dreamina_video_flow.ps1') -Action help | ConvertFrom-Json
    $libtv = & pwsh -NoProfile -File (Join-Path $PSScriptRoot '..\libtv_video_flow.ps1') -Action help | ConvertFrom-Json
    (@($dreamina.actions) -contains 'prepare-video-config') | Should Be $true
    (@($libtv.actions) -contains 'prepare-video-config') | Should Be $true
  }

  It 'does not allow the panel submit tool to bypass confirmation' {
    $controller = Join-Path $PSScriptRoot '..\dreamina_video_flow.ps1'
    $result = & pwsh -NoProfile -File $controller -Action submit -Adapter canvas -RunId ('b' * 32) | ConvertFrom-Json
    $result.status | Should Be 'error'
  }
}
