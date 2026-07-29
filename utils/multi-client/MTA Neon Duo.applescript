property vmName : "Windows 11"
property prlctlPath : "/usr/local/bin/prlctl"

on run
	try
		do shell script "test -x " & quoted form of prlctlPath

		set runningCount to (runPowerShell("@(Get-Process -Name 'gta_sa' -ErrorAction SilentlyContinue).Count", false) as integer)
		if runningCount is 1 then
			display alert "Une session MTA est déjà ouverte" message "Ferme le client restant, puis relance MTA Neon Duo. Le lanceur ne devine pas si la fenêtre survivante est le client principal ou CL2." as warning
			return
		else if runningCount is greater than 2 then
			display alert "Trop de clients MTA sont ouverts" message "Ferme les clients existants avant d'utiliser le lanceur duo." as warning
			return
		end if

		compileLayoutHelper()

		if runningCount is 2 then
			tileClients()
			display notification "Les deux fenêtres existantes vont être replacées côte à côte." with title "MTA Neon Duo"
			return
		end if

		prepareRuntimeProfiles()

		set clientPath to "C:\\dev\\mtasa-vm-custom\\Bin\\Multi Theft Auto.exe"
		set clientDirectory to "C:\\dev\\mtasa-vm-custom\\Bin"
		set serverUri to "mtasa://127.0.0.1:22003"

		set launchPrimary to "Start-Process -FilePath '" & clientPath & "' -ArgumentList '" & serverUri & "' -WorkingDirectory '" & clientDirectory & "'"
		runPowerShell(launchPrimary, true)
		delay 8

		set launchSecondary to "Start-Process -FilePath '" & clientPath & "' -ArgumentList '-cl2 " & serverUri & "' -WorkingDirectory '" & clientDirectory & "'"
		runPowerShell(launchSecondary, true)
		tileClients()

		display notification "Les deux clients démarrent et seront placés côte à côte dès que GTA sera prêt." with title "MTA Neon Duo"
	on error errorMessage number errorNumber
		display alert "Impossible de lancer MTA Neon Duo" message errorMessage & return & "Erreur " & errorNumber as critical
	end try
end run

on compileLayoutHelper()
	set sourcePath to "C:\\Mac\\Home\\Documents\\GitHub\\mtasa-neon\\utils\\multi-client\\DualMtaTile.cs"
	set outputPath to "C:\\dev\\mtasa-vm-custom\\Build\\dual-mta-tile.exe"
	set commandText to "$ErrorActionPreference='Stop'; $source='" & sourcePath & "'; $output='" & outputPath & "'; if (!(Test-Path -LiteralPath $source)) { throw 'DualMtaTile.cs is missing' }; if (!(Test-Path -LiteralPath $output) -or (Get-Item -LiteralPath $source).LastWriteTimeUtc -gt (Get-Item -LiteralPath $output).LastWriteTimeUtc) { Remove-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue; Add-Type -Path $source -OutputAssembly $output -OutputType ConsoleApplication }"
	runPowerShell(commandText, false)
end compileLayoutHelper

on prepareRuntimeProfiles()
	set commandText to "$ErrorActionPreference='Stop'; $base='C:\\dev\\mtasa-vm-custom\\Bin\\MTA\\config'; $primary=Join-Path $base 'coreconfig.xml'; $secondary=Join-Path $base 'coreconfig-cl2.xml'; if (!(Test-Path -LiteralPath $primary)) { throw 'coreconfig.xml is missing' }; if (!(Test-Path -LiteralPath $secondary)) { Copy-Item -LiteralPath $primary -Destination $secondary }; $utf8=New-Object Text.UTF8Encoding($false); foreach ($path in @($primary,$secondary)) { $backup=$path+'.before-dual-window.bak'; if (!(Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $path -Destination $backup }; $content=[IO.File]::ReadAllText($path); $content=[regex]::Replace($content,'<display_windowed>[^<]*</display_windowed>','<display_windowed>1</display_windowed>'); $content=[regex]::Replace($content,'<display_fullscreen_style>[^<]*</display_fullscreen_style>','<display_fullscreen_style>0</display_fullscreen_style>'); $content=[regex]::Replace($content,'<display_resolution>[^<]*</display_resolution>','<display_resolution>1440x900x32</display_resolution>'); if ($path -eq $secondary) { $match=[regex]::Match($content,'<nick>([^<]*)</nick>'); if ($match.Success) { $nick=$match.Groups[1].Value; if ($nick.EndsWith('_CL2')) { $secondaryNick=$nick } elseif ($nick.Length -gt 18) { $secondaryNick=$nick.Substring(0,18)+'_CL2' } else { $secondaryNick=$nick+'_CL2' }; $content=[regex]::Replace($content,'<nick>[^<]*</nick>','<nick>'+$secondaryNick+'</nick>') } }; [IO.File]::WriteAllText($path,$content,$utf8) }; $serverConfig='C:\\dev\\mtasa-vm-custom\\Bin\\server\\mods\\deathmatch\\mtaserver.conf'; if (Test-Path -LiteralPath $serverConfig) { $backup=$serverConfig+'.before-multiclient.bak'; if (!(Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $serverConfig -Destination $backup }; $content=[IO.File]::ReadAllText($serverConfig); $content=$content.Replace('<check_duplicate_serials>1</check_duplicate_serials>','<check_duplicate_serials>0</check_duplicate_serials>'); [IO.File]::WriteAllText($serverConfig,$content,$utf8) }"
	runPowerShell(commandText, false)
end prepareRuntimeProfiles

on tileClients()
	set commandText to "Start-Process -FilePath 'C:\\dev\\mtasa-vm-custom\\Build\\dual-mta-tile.exe'"
	runPowerShell(commandText, true)
end tileClients

on runPowerShell(commandText, currentUser)
	set currentUserOption to ""
	if currentUser then set currentUserOption to " --current-user"
	set shellCommand to prlctlPath & " exec " & quoted form of vmName & currentUserOption & " powershell.exe -NoProfile -Command " & quoted form of commandText
	return do shell script shellCommand
end runPowerShell
