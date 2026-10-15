Describe 'Seedance LibTV video flow controller' {
    $controller = Join-Path $PSScriptRoot '..\libtv_video_flow.ps1'
    . $controller -Action help

    It 'allows only legal state transitions' {
        Test-FlowTransition -From 'prompt_ready' -To 'profile_selected' | Should Be $true
        Test-FlowTransition -From 'prompt_ready' -To 'submitted' | Should Be $false
        Test-FlowTransition -From 'running' -To 'completed' | Should Be $true
    }

    It 'rejects a second submit for the same run' {
        $state = New-FlowState -RunId 'run-test-1' -Profile 'primary' -Project 'project-1' -Prompt 'demo'
        $state.stage = 'submitted'
        Test-CanSubmit -State $state | Should Be $false
    }

    It 'creates dynamic parameter options from a model schema' {
        $schema = [pscustomobject]@{
            properties = [pscustomobject]@{
                ratio = [pscustomobject]@{ enum = @('16:9', '9:16') }
                resolution = [pscustomobject]@{ enum = @([pscustomobject]@{ value = '720p'; displayName = '720P' }) }
                duration = [pscustomobject]@{ min = 4; max = 12; step = 1 }
                modeType = [pscustomobject]@{ items = [pscustomobject]@{ singleImage2video = @(1, 1) } }
            }
            config = [pscustomobject]@{ settings = [pscustomobject]@{ singleImage2video = @('ratio', 'resolution', 'duration') } }
        }
        $options = Get-DynamicParameterOptions -Schema $schema -ModeType 'singleImage2video'
        (@($options.options.ratio.values) -join '|') | Should Match '16:9'
        (@($options.options.resolution.values) -join '|') | Should Match '720p'
        $options.options.duration.min | Should Be 4
        $options.options.duration.max | Should Be 12
    }

    It 'strictly rejects unknown required schema fields' {
        $schema = [pscustomobject]@{
            properties = [pscustomobject]@{
                ratio = [pscustomobject]@{ enum = @('16:9') }
                futureRequired = [pscustomobject]@{ required = $true }
                modeType = [pscustomobject]@{ items = [pscustomobject]@{ singleImage2video = @(1, 1) } }
            }
            config = [pscustomobject]@{ settings = [pscustomobject]@{ singleImage2video = @('ratio', 'futureRequired') } }
        }
        $result = Test-GenerationPlan -Schema $schema -ModeType 'singleImage2video' -Parameters ([pscustomobject]@{ ratio = '16:9' }) -Prompt 'demo' -AssetRefs @([pscustomobject]@{ nodeKey = 'image-1'; type = 'image' })
        $result.valid | Should Be $false
        $result.reason | Should Match 'futureRequired'
    }

    It 'validates a supported plan' {
        $schema = [pscustomobject]@{
            properties = [pscustomobject]@{
                ratio = [pscustomobject]@{ enum = @('16:9') }
                resolution = [pscustomobject]@{ enum = @('720p') }
                duration = [pscustomobject]@{ min = 4; max = 12; step = 1 }
                modeType = [pscustomobject]@{ items = [pscustomobject]@{ singleImage2video = @(1, 1) } }
            }
            config = [pscustomobject]@{ settings = [pscustomobject]@{ singleImage2video = @('ratio', 'resolution', 'duration') } }
        }
        $result = Test-GenerationPlan -Schema $schema -ModeType 'singleImage2video' -Parameters ([pscustomobject]@{ ratio = '16:9'; resolution = '720p'; duration = 8 }) -Prompt 'demo' -AssetRefs @([pscustomobject]@{ nodeKey = 'image-1'; type = 'image' })
        $result.valid | Should Be $true
    }

    It 'uses the plan run directory required by the controller contract' {
        (Get-RunPath 'run-path-test') | Should Match '\\.seedance-director\\video-runs\\run-path-test\.json$'
    }

    It 'does not treat optional schema fields as required' {
        $schema = [pscustomobject]@{
            properties = [pscustomobject]@{
                ratio = [pscustomobject]@{ enum = @('16:9') }
                optionalField = [pscustomobject]@{ enum = @('a', 'b') }
                modeType = [pscustomobject]@{ items = [pscustomobject]@{ text2video = @(0, 0) } }
            }
            required = @('ratio')
            config = [pscustomobject]@{ settings = [pscustomobject]@{ text2video = @('ratio', 'optionalField') } }
        }
        $result = Test-GenerationPlan -Schema $schema -ModeType 'text2video' -Parameters ([pscustomobject]@{ ratio = '16:9' }) -Prompt 'demo'
        $result.valid | Should Be $true
    }

    It 'enforces mode input counts and per-media limits' {
        $schema = [pscustomobject]@{
            properties = [pscustomobject]@{
                ratio = [pscustomobject]@{ enum = @('16:9') }
                modeType = [pscustomobject]@{
                    items = [pscustomobject]@{ mixed2video = @(1, 3) }
                    mixed2videoConfig = [pscustomobject]@{ imageMax = 1; videoMax = 1; audioMax = 1 }
                }
            }
            config = [pscustomobject]@{ settings = [pscustomobject]@{ mixed2video = @('ratio') } }
        }
        $tooManyImages = [pscustomobject]@{
            ratio = '16:9'
        }
        $assets = @(
            [pscustomobject]@{ nodeKey = 'img-1'; type = 'image' }
            [pscustomobject]@{ nodeKey = 'img-2'; type = 'image' }
        )
        $result = Test-GenerationPlan -Schema $schema -ModeType 'mixed2video' -Parameters $tooManyImages -Prompt 'demo' -AssetRefs $assets
        $result.valid | Should Be $false
        $result.reason | Should Match 'image'
    }

    It 'maps terminal node states to flow stages' {
        (Get-NodeLifecycleStage ([pscustomobject]@{ status = 'processing' })) | Should Be 'running'
        (Get-NodeLifecycleStage ([pscustomobject]@{ taskStatus = 'succeeded' })) | Should Be 'completed'
        (Get-NodeLifecycleStage ([pscustomobject]@{ state = 'failed' })) | Should Be 'failed'
        (Get-NodeLifecycleStage ([pscustomobject]@{ nodes = @([pscustomobject]@{ status = 'completed' }) })) | Should Be 'completed'
    }

    It 'resumes early runs without querying a nonexistent node' {
        (Get-ResumeDecision ([pscustomobject]@{ stage = 'prompt_ready' })) | Should Be 'restart_selection'
        (Get-ResumeDecision ([pscustomobject]@{ stage = 'awaiting_final_confirmation' })) | Should Be 'show_confirmation'
        (Get-ResumeDecision ([pscustomobject]@{ stage = 'running'; nodeKey = 'node-1' })) | Should Be 'query_original'
    }

    It 'marks a profile with a failed account check as unavailable' {
        $record = New-ProfileStatusRecord -ProfileId 'account-3' -ConfigDir 'C:\profile' -Info $null -ExitCode 1 -Raw '401 not logged in'
        $record.status | Should Be 'error'
        $record.error | Should Match '401'
    }

    It 'detects a changed model schema before resuming confirmation' {
        (Test-SchemaFingerprint -State ([pscustomobject]@{ schemaHash = 'old' }) -SchemaResult ([pscustomobject]@{ schemaHash = 'new' })) | Should Be $true
        (Test-SchemaFingerprint -State ([pscustomobject]@{ schemaHash = 'same' }) -SchemaResult ([pscustomobject]@{ schemaHash = 'same' })) | Should Be $false
    }

    It 'does not silently accept an invalid cached DH folder mapping' {
        (Get-ProjectResultStatus -UsedCachedFolder $true -ProjectCount 0) | Should Be 'folder_mapping_invalid'
        (Get-ProjectResultStatus -UsedCachedFolder $false -ProjectCount 0) | Should Be 'no_projects'
        (Get-ProjectResultStatus -UsedCachedFolder $true -ProjectCount 2) | Should Be 'ready'
    }
}
