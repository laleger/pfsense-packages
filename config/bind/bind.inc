<?PHP
/* $Id$ */
/*
	bind.inc
	part of the Bind package for pfSense
	Copyright (C) 2013 Juliano Oliveira/Adriano Brancher
	Copyright (C) 2013 Marcello Coutinho  
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	1. Redistributions of source code must retain the above copyright notice,
	   this list of conditions and the following disclaimer.

	2. Redistributions in binary form must reproduce the above copyright
	   notice, this list of conditions and the following disclaimer in the
	   documentation and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
	INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
	AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
	AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
	OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.

*/
$shortcut_section = "bind";
require_once('globals.inc');
require_once('config.inc');
require_once('util.inc');
require_once('pfsense-utils.inc');
require_once('pkg-utils.inc');
require_once('service-utils.inc');
if(!function_exists("filter_configure")) 
	require_once("filter.inc");

$pf_version=substr(trim(file_get_contents("/etc/version")),0,3);
if ($pf_version > 2.0)
	define('BIND_LOCALBASE', '/usr/pbi/bind-' . php_uname("m"));
else
  define('BIND_LOCALBASE','/usr/local');

define('CHROOT_LOCALBASE','/conf/named');

function bind_zone_validate($post, $input_errors){
	if ($_POST['type']=='reverso'){
		if( $_POST['nameserver'] == "")
			$input_errors[] = 'The field \'Name Server\' is required';
		}
	if (key_exists("mail",$_POST))
		$_POST['mail']=preg_replace("/@/",".",$post['mail']);
	for ($i=0;$i < count($_POST);$i++){
		if (key_exists("hostname$i",$_POST)){
			if (preg_match("/(MX|NS)/",$_POST["hosttype$i"]))
				$_POST["hostname$i"]="";
			if (!preg_match("/(MX|NS)/",$_POST["hosttype$i"]) && $_POST["hostname$i"]=="")
				$input_errors[] = 'Record cannot be empty for '.$_POST["hosttype$i"].' type ';
			if ($_POST["hosttype$i"]=="MX" && $_POST["hostvalue$i"]=="")
				$_POST["hostvalue$i"]="10";
			if ($_POST["hosttype$i"]!="MX" && $_POST["hostvalue$i"]!="")
				$_POST["hostvalue$i"]="";
			if ($_POST["hostdst$i"]=="")
				$input_errors[] = 'Alias or IP address cannot be empty.';
			}
		}
}

 function bind_sync(){

	global $config;

	$bind = $config["installedpackages"]["bind"]["config"][0]; 
	$bind_enable = $bind['enable_bind'];
	$bind_forwarder = $bind['bind_forwarder'];
	$forwarder_ips = $bind['bind_forwarder_ips'];
	$ram_limit = ($bind['bind_ram_limit']?$bind['bind_ram_limit']:"256M");
	$hide_version = $bind['bind_hide_version'];
	$bind_notify = $bind['bind_notify'];
	$custom_options = base64_decode($bind['bind_custom_options']);
	$bind_logging = $bind['bind_logging'];
	$bind_conf ="#Bind pfsense configuration\n"; 
	$bind_conf .="#Do not edit this file!!!\n"; 
$bind_conf .= <<<EOD

	options {
		directory "/etc/namedb";
		pid-file "/var/run/named/pid";
		statistics-file "/var/log/named.stats";
		max-cache-size {$ram_limit};
		query-source address * port 53;

EOD;
	// check response rate limit option
	//https://kb.isc.org/article/AA-01000/0/A-Quick-Introduction-to-Response-Rate-Limiting.html
	//http://ss.vix.su/~vjs/rl-arm.html
	if ($bind['rate_enabled']=="on"){
		$rate_limit=($bind['rate_limit']?$bind['rate_limit']:"15");
		$log_only=($bind['log_only']=="no"?"no":"yes");
		$bind_conf .= <<<EOD
		rate-limit {
			responses-per-second {$rate_limit};
			log-only {$log_only};
    		};
    	
EOD;
	}
	//check ips to listen on
	if (preg_match("/All/",$bind['listenon'])){
		$bind_listenonv6="Any;";
		$bind_listenon="Any;";
		}
	else{
		$bind_listenonv6="";
		$bind_listenon ="";
		foreach (explode(',',$bind['listenon']) as $listenon){
			if (is_ipaddrv6($listenon))
				$bind_listenonv6 .= $listenon."; ";
			elseif (is_ipaddr($listenon))
				$bind_listenon .= $listenon."; ";
			else{
				$listenon=(pfSense_get_interface_addresses(convert_friendly_interface_to_real_interface_name($listenon)));
				if (is_ipaddr($listenon['ipaddr']))
					$bind_listenon .= $listenon['ipaddr']."; ";
				elseif(is_ipaddrv6($listenon['ipaddr']))
					$bind_listenonv6 .= $listenon['ipaddr']."; ";
			}	
		}
	}
	$bind_listenonv6=($bind_listenonv6==""?"none;":$bind_listenonv6);
	$bind_listenon=($bind_listenon==""?"none;":$bind_listenon);
	//print "<PRE>$bind_listenonv6 $bind_listenon";
	if (key_exists("ipv6allow",$config['system'])){
		$bind_conf .="\t\tlisten-on-v6 { $bind_listenonv6 };\n";
		}
	$bind_conf .="\t\tlisten-on { $bind_listenon };\n";

	#forwarder config
	if ($bind_forwarder == on)
		$bind_conf .="\t\tforwarders { $forwarder_ips };\n";
	if ($bind_notify == on)
		$bind_conf .="\t\tnotify yes;\n"; 
	if ($hide_version == on)
		$bind_conf .="\t\tversion \"N/A\";\n"; 

	$bind_conf .="\t\t$custom_options\n"; 
	$bind_conf .= "\t};\n\n";
	
	if ($bind_logging == on){
$bind_conf .= <<<EOD

	logging {
		channel custom {
			file "/var/log/named.log";
			print-time yes;
			print-category yes;
		};

		category config         {custom;};
		category notify         {custom;};
		category dnssec         {custom;};
		category general        {custom;};
		category security       {custom;};
		category xfer-out       {custom;};
		category lame-servers   {custom;};
	}; 

EOD;
	}

	#Config Zone domain
	if(!is_array($config["installedpackages"]["bindacls"]) || !is_array($config["installedpackages"]["bindacls"]["config"])){
		$config["installedpackages"]["bindacls"]["config"][] =array("name"=>"any","description"=>"Default Access list","row" => array("value"=> "","description"=>""));
		write_config();
		}
	$bindacls = $config["installedpackages"]["bindacls"]["config"];
	for ($i=0; $i<sizeof($bindacls); $i++)
	{
		$aclname = $bindacls[$i]['name'];
		$aclhost = $bindacls[$i]['row'];
		if($aclname != "any"){
			$bind_conf .= "acl \"$aclname\" {\n";	
			for ($u=0; $u<sizeof($aclhost); $u++)
			{
				$aclhostvalue = $aclhost[$u]['value'];
				$bind_conf .= "\t$aclhostvalue;\n";
			}
			$bind_conf .= "};\n\n";
		}	
	}
 
	if(is_array($config["installedpackages"]["bindviews"])) 
		$bindview = $config["installedpackages"]["bindviews"]["config"];
	else
		$bindview =array();
	 
	for ($i=0; $i<sizeof($bindview); $i++) 
	{	
		$views = $config["installedpackages"]["bindviews"]["config"][$i];
		$viewname = $views['name']; 	
		$viewrecursion = $views['recursion']; 
		if($views['match-clients'] == '')
			$viewmatchclients = "none"; 	
		else
			$viewmatchclients = str_replace(',','; ',$views['match-clients']); 	
		if($views['allow-recursion'] == '')
			$viewallowrecursion = "none"; 	
		else
			$viewallowrecursion = str_replace(',','; ',$views['allow-recursion']); 	
		$viewcustomoptions = $views['bind_custom_options']; 	
		
		$bind_conf .= "view \"$viewname\" { \n\n"; 
		$bind_conf .= "\trecursion $viewrecursion;\n"; 
		$bind_conf .= "\tmatch-clients { $viewmatchclients;};\n"; 
		$bind_conf .= "\tallow-recursion { $viewallowrecursion;};\n"; 
		$bind_conf .= "\t$viewcustomoptions\n\n"; 
		
		$bind_conf .="\tzone \".\" {\n";
		$bind_conf .="\t\ttype hint;\n";
		$bind_conf .="\t\tfile \"/etc/namedb/named.root\";\n";
		$bind_conf .= "\t};\n\n";
	
		if(is_array($config["installedpackages"]["bindzone"])) 
		$bindzone = $config["installedpackages"]["bindzone"]["config"];
	else
		$bindzone =array();
		for ($x=0; $x<sizeof($bindzone); $x++)
		{
			$zone = $bindzone[$x];
		
			$zonename = $zone['name'];	
			$zonetype = $zone['type'];	
			$zoneview = $zone['view'];	
			$zonecustom = $zone['custom'];	
			$zoneipslave = $zone['slaveip'];	
			$zonereverso = $zone['reverso'];				
			if($zone['allowupdate'] == '')
				$zoneallowupdate = "none"; 	
			else
				$zoneallowupdate = str_replace(',','; ',$zone['allowupdate']); 	
			if($zone['allowquery'] == '')
				$zoneallowquery = "none"; 	
			else
				$zoneallowquery = str_replace(',','; ',$zone['allowquery']); 	
			if($zone['allowtransfer'] == '')
				$zoneallowtransfer = "none"; 	
			else
				$zoneallowtransfer = str_replace(',','; ',$zone['allowtransfer']); 	

				if ($zoneview == $viewname)
				{
					if($zonereverso == "on")
				 		$bind_conf .= "\tzone \"$zonename.in-addr.arpa\" {\n";	
				  	else	
						$bind_conf .= "\tzone \"$zonename\" {\n";	
        		
					$bind_conf .= "\t\ttype $zonetype;\n";
        				$bind_conf .= "\t\tfile \"/etc/namedb/$zonetype/$zoneview/$zonename.DB\";\n";
				 	if ($zonetype == "slave")		
        					$bind_conf .= "\t\tmasters { $zoneipslave };\n";
				 	if ($zonetype != "slave")		
						$bind_conf .= "\t\tallow-update { $zoneallowupdate;};\n"; 
				 	if ($zonetype != "slave")		
						$bind_conf .= "\t\tallow-query { $zoneallowquery;};\n"; 
				 	if ($zonetype != "slave")		
						$bind_conf .= "\t\tallow-transfer { $zoneallowtransfer;};\n"; 
				 	if ($zonecustom != '')		
        					$bind_conf .= "\t\t$zonecustom\n";
					$bind_conf .= "\t};\n\n";
        
					if ($zonetype == "master"){
						$zonetll = $zone['tll'];				
						$zonemail = preg_replace("/@/",".",$zone['mail']);				
						$zoneserial = $zone['serial'];				
						$zonerefresh = $zone['refresh'];				
						$zoneretry = $zone['retry'];				
						$zoneexpire = $zone['expire'];				
						$zoneminimum = $zone['minimum'];				
						$zonenameserver = $zone['nameserver'];				
						$zoneipns = $zone['ipns'];				
						$zonereverso = $zone['reverso'];				
						if($zone['allowupdate'] == '')
							$zoneallowupdate = "none"; 	
						else
							$zoneallowupdate = str_replace(',','; ',$zone['allowupdate']); 	
						if($zone['allowquery'] == '')
							$zoneallowquery = "none"; 	
						else
							$zoneallowquery = str_replace(',','; ',$zone['allowquery']); 	
						if($zone['allowtransfer'] == '')
							$zoneallowtransfer = "none"; 	
						else
							$zoneallowtransfer = str_replace(',','; ',$zone['allowtransfer']); 		
						$zone_conf = "\$TTL $zonetll\n;\n";
						$zone_conf .= ";\tDatabase file {$zonename}.DB for {$zonename} zone.\n";
						$zone_conf .= ";\tDo not edit this file!!!\n";
						$zone_conf .= ";\tZone version {$zoneserial}\n;\n";
					 	if($zonereverso == "on")
							$zone_conf .= "@\t IN  SOA $zonenameserver. \t $zonemail. (\n";	
						else
							$zone_conf .= "$zonename.\t IN  SOA $zonenameserver. \t $zonemail. (\n";	

						$zone_conf .= "\t\t$zoneserial ; serial\n";	
						$zone_conf .= "\t\t$zonerefresh ; refresh\n";	
						$zone_conf .= "\t\t$zoneretry ; retry\n";	
						$zone_conf .= "\t\t$zoneexpire ; expire\n";	
						$zone_conf .= "\t\t$zoneminimum ; default_ttl\n\t\t)\n\n";
						$zone_conf .= ";\n; Zone Records\n;\n";

					 	if($zonereverso == "on")
							$zone_conf .= "\t IN NS \t\t$zonenameserver.\n";	
						else{	
							$zone_conf .= "@ \t IN NS \t\t$zonenameserver.\n";
							if ($zoneipns !="")
								$zone_conf .= "@ \t IN A \t\t$zoneipns\n";
						}
						for ($y=0; $y<sizeof($zone['row']); $y++)
						{
							$hostname = (preg_match("/(MX|NS)/",$zone['row'][$y]['hosttype'])?"@":$zone['row'][$y]['hostname']);
							$hosttype = $zone['row'][$y]['hosttype'];
							$hostdst = $zone['row'][$y]['hostdst'];
							if (preg_match("/[a-zA-Z]/",$hostdst) && $hosttype !="TXT")
								$hostdst .= ".";
							$hostvalue = $zone['row'][$y]['hostvalue'];
							
							$zone_conf .= "$hostname \t IN $hosttype $hostvalue \t$hostdst\n";	
						}	
						if (!(is_dir(CHROOT_LOCALBASE."/etc/namedb/$zonetype/$zoneview"))) {
            					mkdir(CHROOT_LOCALBASE."/etc/namedb/$zonetype/$zoneview",0755,true);
					}
						file_put_contents(CHROOT_LOCALBASE."/etc/namedb/$zonetype/$zoneview/$zonename.DB", $zone_conf);
			
					}
				}		
		}

		$bind_conf .= "};\n"; 
	}
	$dirs=array("/etc/namedb","/var/run/named","/var/dump","/var/log","/var/stats");
	foreach ($dirs as $dir){
		if (!is_dir(CHROOT_LOCALBASE .$dir))
			mkdir(CHROOT_LOCALBASE .$dir,0755,true);
		}
	file_put_contents(CHROOT_LOCALBASE.'/etc/namedb/named.conf', $bind_conf);
	if (!file_exists(CHROOT_LOCALBASE."/etc/namedb/named.root")){
		$named_root=file_get_contents("http://www.internic.net/domain/named.root");
		file_put_contents(CHROOT_LOCALBASE."/etc/namedb/named.root",$named_root,LOCK_EX);
	}

	bind_write_rcfile();
	chown(CHROOT_LOCALBASE."/var/log","bind");
	chgrp(CHROOT_LOCALBASE."/var/log","bind");
 	if($bind_enable == "on")
 		mwexec("/usr/local/etc/rc.d/named.sh restart");
 	else
 		mwexec("/usr/local/etc/rc.d/named.sh stop");
}

function bind_print_javascript_type_zone(){
?>
        <script language="JavaScript">
        <!--
        function on_type_zone_changed() {

		var field = document.iform.type;
       		var tipo = field.options[field.selectedIndex].value;
        		if (tipo == 'master') {
		        	
       				document.iform.slaveip.disabled = 1;
                                document.iform.tll.disabled = 0;
                                document.iform.nameserver.disabled = 0;
                                document.iform.ipns.disabled = 0;
                                document.iform.mail.disabled = 0;
                                document.iform.serial.disabled = 0;
                                document.iform.refresh.disabled = 0;
                                document.iform.retry.disabled = 0;
                                document.iform.expire.disabled = 0;
                                document.iform.minimum.disabled = 0;
               
        		}
        		else {
       				document.iform.slaveip.disabled = 0;
                                document.iform.tll.disabled = 1;
                                document.iform.nameserver.disabled = 1;
                                document.iform.ipns.disabled = 1;
                                document.iform.mail.disabled = 1;
                                document.iform.serial.disabled = 1;
                                document.iform.refresh.disabled = 1;
                                document.iform.retry.disabled = 1;
                                document.iform.expire.disabled = 1;
                                document.iform.minimum.disabled = 1;
			}
        }
        -->
        </script>
<?php
}

function bind_print_javascript_type_zone2(){
        print("<script language=\"JavaScript\">on_type_zone_changed()</script>\n");
}

function bind_write_rcfile() {
        $rc = array();
        $BIND_LOCALBASE = "/usr/local";
        $rc['file'] = 'named.sh';
        $rc['start'] = <<<EOD
if [ -z "`ps auxw | grep "[n]amed -c /etc/namedb/named.conf"|awk '{print $2}'`" ];then
        {$BIND_LOCALBASE}/sbin/named -c /etc/namedb/named.conf -u bind -t /conf/named/
fi

EOD;
        $rc['stop'] = <<<EOD
killall -9 named 2>/dev/null
sleep 2
EOD;
        $rc['restart'] = <<<EOD
if [ -z "`ps auxw | grep "[n]amed -c /etc/namedb/named.conf"|awk '{print $2}'`" ];then
        	{$BIND_LOCALBASE}/sbin/named -c /etc/namedb/named.conf -u bind -t /conf/named/
        else
		killall -9 named 2>/dev/null
         	sleep 3	
        	{$BIND_LOCALBASE}/sbin/named -c /etc/namedb/named.conf -u bind -t /conf/named/
        fi

EOD;
        conf_mount_rw();
        write_rcfile($rc);
        conf_mount_ro();
}

function create_log_file(){
 	mwexec("touch ".CHROOT_LOCALBASE."/var/log/named.log");
 	mwexec("chown bind:bind ".CHROOT_LOCALBASE."/var/log/named.log");
 	mwexec("chmod 755 ".CHROOT_LOCALBASE."/var/log/named.log");
}

function delete_log_file(){
 	mwexec("rm ".CHROOT_LOCALBASE."/var/log/named.log");
}
/* Uses XMLRPC to synchronize the changes to a remote node */
function bind_sync_on_changes() {
	global $config, $g;
	if (is_array($config['installedpackages']['bind']['config'])){
		$bind_sync=$config['installedpackages']['bind']['config'][0];
		$synconchanges = $bind_sync['synconchanges'];
		$synctimeout = $bind_sync['synctimeout'];
		switch ($synconchanges){
			case "manual":
				if (is_array($bind_sync[row])){
					$rs=$bind_sync[row];
					}
				else{
					log_error("[bind] xmlrpc sync is enabled but there is no hosts to push on bind config.");
					return;
					}
				break;
			case "auto":
					if (is_array($config['installedpackages']['carpsettings']) && is_array($config['installedpackages']['carpsettings']['config'])){
						$system_carp=$config['installedpackages']['carpsettings']['config'][0];
						$rs[0]['ipaddress']=$system_carp['synchronizetoip'];
						$rs[0]['username']=$system_carp['username'];
						$rs[0]['password']=$system_carp['password'];
					}
					else{
						log_error("[bind] xmlrpc sync is enabled but there is no system backup hosts to push bind config.");
						return;
					}
				break;			
			default:
				return;
			break;
		}
		if (is_array($rs)){
			log_error("[bind] xmlrpc sync is starting.");
			foreach($rs as $sh){
				$sync_to_ip = $sh['ipaddress'];
				$password = $sh['password'];
				if($sh['username'])
					$username = $sh['username'];
				else
					$username = 'admin';
				if($password && $sync_to_ip)
					bind_do_xmlrpc_sync($sync_to_ip, $username, $password,$synctimeout);
				}
			log_error("[bind] xmlrpc sync is ending.");
			}
 		}
}
/* Do the actual XMLRPC sync */
function bind_do_xmlrpc_sync($sync_to_ip, $username, $password, $synctimeout) {
	global $config, $g;

	if(!$username)
		return;
		
	if(!$password)
		return;

	if(!$sync_to_ip)
		return;

	if(!$synctimeout)
		$synctimeout=25;
		
		
	$xmlrpc_sync_neighbor = $sync_to_ip;
    if($config['system']['webgui']['protocol'] != "") {
		$synchronizetoip = $config['system']['webgui']['protocol'];
		$synchronizetoip .= "://";
    }
    $port = $config['system']['webgui']['port'];
    /* if port is empty lets rely on the protocol selection */
    if($port == "") {
		if($config['system']['webgui']['protocol'] == "http") 
			$port = "80";
		else 
			$port = "443";
    }
	$synchronizetoip .= $sync_to_ip;

	/* xml will hold the sections to sync */
	$xml = array();
	$xml['bind'] = $config['installedpackages']['bind'];
	$xml['bindacls'] = $config['installedpackages']['bindacls'];
	$xml['bindviews'] = $config['installedpackages']['bindviews'];
	$xml['bindzone'] = $config['installedpackages']['bindzone'];
	/* assemble xmlrpc payload */
	$params = array(
		XML_RPC_encode($password),
		XML_RPC_encode($xml)
	);

	/* set a few variables needed for sync code borrowed from filter.inc */
	$url = $synchronizetoip;
	log_error("[bind] Beginning bind XMLRPC sync to {$url}:{$port}.");
	$method = 'pfsense.merge_installedpackages_section_xmlrpc';
	$msg = new XML_RPC_Message($method, $params);
	$cli = new XML_RPC_Client('/xmlrpc.php', $url, $port);
	$cli->setCredentials($username, $password);
	if($g['debug'])
		$cli->setDebug(1);
	/* send our XMLRPC message and timeout after defined sync timeout value*/
	$resp = $cli->send($msg, $synctimeout);
	if(!$resp) {
		$error = "A communications error occurred while attempting bind XMLRPC sync with {$url}:{$port}.";
		log_error($error);
		file_notice("sync_settings", $error, "bind Settings Sync", "");
	} elseif($resp->faultCode()) {
		$cli->setDebug(1);
		$resp = $cli->send($msg, $synctimeout);
		$error = "An error code was received while attempting bind XMLRPC sync with {$url}:{$port} - Code " . $resp->faultCode() . ": " . $resp->faultString();
		log_error($error);
		file_notice("sync_settings", $error, "bind Settings Sync", "");
	} else {
		log_error("[bind] XMLRPC sync successfully completed with {$url}:{$port}.");
	}
	
	/* tell bind to reload our settings on the destination sync host. */
	$method = 'pfsense.exec_php';
	$execcmd  = "require_once('/usr/local/pkg/bind.inc');\n";
	$execcmd .= "bind_sync('yes');";
	/* assemble xmlrpc payload */
	$params = array(
		XML_RPC_encode($password),
		XML_RPC_encode($execcmd)
	);
	
	log_error("[bind] XMLRPC reload data {$url}:{$port}.");
	$msg = new XML_RPC_Message($method, $params);
	$cli = new XML_RPC_Client('/xmlrpc.php', $url, $port);
	$cli->setCredentials($username, $password);
	$resp = $cli->send($msg, $synctimeout);
	if(!$resp) {
		$error = "A communications error occurred while attempting bind XMLRPC sync with {$url}:{$port} (pfsense.exec_php).";
		log_error($error);
		file_notice("sync_settings", $error, "Bind Settings Sync", "");
	} elseif($resp->faultCode()) {
		$cli->setDebug(1);
		$resp = $cli->send($msg, $synctimeout);
		$error = "[Bind] An error code was received while attempting bind XMLRPC sync with {$url}:{$port} - Code " . $resp->faultCode() . ": " . $resp->faultString();
		log_error($error);
		file_notice("sync_settings", $error, "bind Settings Sync", "");
	} else {
		log_error("Bind XMLRPC reload data success with {$url}:{$port} (pfsense.exec_php).");
	}
	
}
?>
