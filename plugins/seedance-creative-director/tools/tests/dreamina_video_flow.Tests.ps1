Describe 'Seedance Dreamina dual CLI flow controller' {
  $controller = Join-Path $PSScriptRoot '..\dreamina_video_flow.ps1'

  It 'advertises separate canvas and conversation adapters' {
    $result = & pwsh -NoProfile -File $controller -Action help | ConvertFrom-Json
    (@($result.adapters) -contains 'canvas') | Should Be $true
    (@($result.adapters) -contains 'conversation') | Should Be $true
  }

  It 'keeps run records under the Dreamina-specific director directory' {
    $result = & pwsh -NoProfile -File $controller -Action help | ConvertFrom-Json
    $result.stateRoot | Should Match '\\.seedance-director\\dreamina-runs$'
  }

  It 'does not submit without final confirmation' {
    $result = & pwsh -NoProfile -File $controller -Action submit -RunId ('a' * 32) | ConvertFrom-Json
    $result.status | Should Be 'error'
  }

  It 'rejects an unrecognized adapter before any CLI call' {
    $result = & pwsh -NoProfile -File $controller -Action list-containers -Adapter invalid -Profile primary 2>&1
    $LASTEXITCODE | Should Not Be 0
  }
}
