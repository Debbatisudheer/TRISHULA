pipeline {
    agent { label 'trishula-windows-ucrt64' }

    options {
        timestamps()
        disableConcurrentBuilds()
        buildDiscarder(logRotator(numToKeepStr: '20'))
        skipDefaultCheckout(true)
    }

    environment {
        UCRT64_BIN = 'C:/msys64/ucrt64/bin'
        CPP_BUILD_DIR = 'build_jenkins'
    }

    stages {

        stage('Checkout') {
            steps {
                checkout scm
            }
        }

        stage('Toolchain Preflight') {
            steps {
                powershell '''
                    $ErrorActionPreference = "Stop"

                    Write-Host "========================================"
                    Write-Host "TRISHULA TOOLCHAIN PREFLIGHT"
                    Write-Host "========================================"

                    $ucrt = $env:UCRT64_BIN

                    if (-not (Test-Path "$ucrt\\cmake.exe")) {
                        throw "CMake not found: $ucrt\\cmake.exe"
                    }

                    if (-not (Test-Path "$ucrt\\ctest.exe")) {
                        throw "CTest not found: $ucrt\\ctest.exe"
                    }

                    if (-not (Test-Path "$ucrt\\g++.exe")) {
                        throw "g++ not found: $ucrt\\g++.exe"
                    }

                    $env:PATH = "$ucrt;$env:PATH"

                    & "$ucrt\\g++.exe" --version
                    if ($LASTEXITCODE -ne 0) {
                        throw "g++ preflight failed."
                    }

                    & "$ucrt\\cmake.exe" --version
                    if ($LASTEXITCODE -ne 0) {
                        throw "CMake preflight failed."
                    }

                    & "$ucrt\\ctest.exe" --version
                    if ($LASTEXITCODE -ne 0) {
                        throw "CTest preflight failed."
                    }

                    go version
                    node --version
                    npm --version

                    Write-Host "Toolchain preflight: PASS"
                '''
            }
        }

        stage('Backend - CMake Configure') {
            steps {
                dir('backend') {
                    powershell '''
                        $ErrorActionPreference = "Stop"

                        $env:PATH = "$env:UCRT64_BIN;$env:PATH"

                        & "$env:UCRT64_BIN\\cmake.exe" `
                            -S . `
                            -B $env:CPP_BUILD_DIR `
                            -G "MinGW Makefiles" `
                            -DCMAKE_BUILD_TYPE=Release

                        if ($LASTEXITCODE -ne 0) {
                            throw "CMake configure failed."
                        }

                        Write-Host "CMake configure: PASS"
                    '''
                }
            }
        }

        stage('Backend - C++ Build') {
            steps {
                dir('backend') {
                    powershell '''
                        $ErrorActionPreference = "Stop"

                        $env:PATH = "$env:UCRT64_BIN;$env:PATH"

                        & "$env:UCRT64_BIN\\cmake.exe" `
                            --build $env:CPP_BUILD_DIR `
                            --parallel 4

                        if ($LASTEXITCODE -ne 0) {
                            throw "C++ build failed."
                        }

                        Write-Host "C++ build: PASS"
                    '''
                }
            }
        }

        stage('Backend - C++ Tests') {
            steps {
                dir('backend') {
                    powershell '''
                        $ErrorActionPreference = "Stop"

                        $env:PATH = "$env:UCRT64_BIN;$env:PATH"

                        & "$env:UCRT64_BIN\\ctest.exe" `
                            --test-dir $env:CPP_BUILD_DIR `
                            --output-on-failure

                        if ($LASTEXITCODE -ne 0) {
                            throw "CTest failed."
                        }

                        Write-Host "C++ tests: PASS"
                    '''
                }
            }
        }

        stage('Ground Data Service - Go Tests') {
            steps {
                dir('backend/services/ground-data-service') {
                    powershell '''
                        $ErrorActionPreference = "Stop"

                        go test ./...

                        if ($LASTEXITCODE -ne 0) {
                            throw "Go tests failed."
                        }

                        Write-Host "Go tests: PASS"
                    '''
                }
            }
        }

        stage('Ground Data Service - Go Vet') {
            steps {
                dir('backend/services/ground-data-service') {
                    powershell '''
                        $ErrorActionPreference = "Stop"

                        go vet ./...

                        if ($LASTEXITCODE -ne 0) {
                            throw "Go vet failed."
                        }

                        Write-Host "Go vet: PASS"
                    '''
                }
            }
        }

        stage('Mission Control UI - Install') {
            steps {
                dir('mission-control-ui') {
                    powershell '''
                        $ErrorActionPreference = "Stop"

                        npm ci

                        if ($LASTEXITCODE -ne 0) {
                            throw "npm ci failed."
                        }

                        Write-Host "npm ci: PASS"
                    '''
                }
            }
        }

        stage('Mission Control UI - TypeScript') {
            steps {
                dir('mission-control-ui') {
                    powershell '''
                        $ErrorActionPreference = "Stop"

                        npx tsc --noEmit

                        if ($LASTEXITCODE -ne 0) {
                            throw "TypeScript validation failed."
                        }

                        Write-Host "TypeScript validation: PASS"
                    '''
                }
            }
        }

        stage('Mission Control UI - Production Build') {
            steps {
                dir('mission-control-ui') {
                    powershell '''
                        $ErrorActionPreference = "Stop"

                        npm run build

                        if ($LASTEXITCODE -ne 0) {
                            throw "Next.js production build failed."
                        }

                        if (-not (Test-Path ".next")) {
                            throw "Next.js .next output was not created."
                        }

                        Write-Host "Next.js production build: PASS"
                    '''
                }
            }
        }

        stage('Package CI Artifacts') {
            steps {
                powershell '''
                    $ErrorActionPreference = "Stop"

                    $artifactDir = "$env:WORKSPACE\\ci-artifacts"

                    New-Item `
                        -ItemType Directory `
                        -Force `
                        -Path $artifactDir |
                        Out-Null

                    $cache = "$env:WORKSPACE\\backend\\$env:CPP_BUILD_DIR\\CMakeCache.txt"

                    if (Test-Path $cache) {
                        Copy-Item `
                            $cache `
                            "$artifactDir\\CMakeCache.txt" `
                            -Force
                    }

                    $buildId = "$env:WORKSPACE\\mission-control-ui\\.next\\BUILD_ID"

                    if (Test-Path $buildId) {
                        Copy-Item `
                            $buildId `
                            "$artifactDir\\UI-BUILD_ID" `
                            -Force
                    }

                    Write-Host "CI artifacts prepared."
                '''

                archiveArtifacts(
                    artifacts: 'ci-artifacts/**, mission-control-ui/package.json, mission-control-ui/package-lock.json',
                    allowEmptyArchive: false,
                    fingerprint: true
                )
            }
        }
    }

    post {
        success {
            echo '========================================'
            echo 'TRISHULA JENKINS CI PASSED'
            echo '========================================'
            echo 'Git checkout: PASS'
            echo 'Toolchain preflight: PASS'
            echo 'CMake configure: PASS'
            echo 'C++ build: PASS'
            echo 'C++ tests: PASS'
            echo 'Go tests: PASS'
            echo 'Go vet: PASS'
            echo 'UI npm ci: PASS'
            echo 'TypeScript: PASS'
            echo 'Next.js build: PASS'
            echo 'Artifacts: PACKAGED'
            echo '========================================'
        }

        failure {
            echo '========================================'
            echo 'TRISHULA JENKINS CI FAILED'
            echo '========================================'
            echo 'TRISHULA source was not modified by Jenkins CI.'
            echo '========================================'
        }

        cleanup {
            deleteDir()
        }
    }
}