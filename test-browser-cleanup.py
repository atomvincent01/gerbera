from pathlib import Path
import subprocess,os
root=Path.cwd()
page=root/'test/e2e/page/home.page.js'
spec=root/'test/e2e/overlay.spec.js'
corrected=page.read_text()
original_spec=spec.read_text()
base=subprocess.check_output(['git','show','783988da97c847dec4227c9c034d24723975f97b:gerbera-web/test/e2e/page/home.page.js']).decode()
# Delay the actual UI control in the reported container-selection test.
# The test body still checks the normal field visibility before cleanup.
needle="""      await itemSelectionTest('container', 'object.container', ["""
start=original_spec.index(needle)
end=original_spec.index('      ]);',start)+len('      ]);')
delay="""
      await driver.executeScript(() => {
        const button = document.getElementById('editCancel');
        button.style.visibility = 'hidden';
        window.setTimeout(() => { button.style.visibility = ''; }, 1800);
      });"""
modified=original_spec[:end]+delay+original_spec[end:]
env=dict(os.environ,npm_package_config_webserver_host='localhost',npm_package_config_webserver_port='3000',SELENIUM_BROWSER='chrome')
server=subprocess.Popen(['node','index.js'],stdout=subprocess.DEVNULL,stderr=subprocess.STDOUT,env=env)
try:
 import urllib.request,time
 for attempt in range(60):
  try:
   urllib.request.urlopen('http://localhost:3000/index.html',timeout=2).close();break
  except OSError:time.sleep(0.2)
 spec.write_text(modified)
 for name,content in [('old',base),('fixed',corrected)]:
  page.write_text(content)
  command=['node_modules/.bin/mocha','--config','test/.mocharc.yml','test/e2e/overlay.spec.js','--grep','only shows type, title and class when adding container type','--retries','0']
  result=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
  (root/('cleanup-'+name+'.log')).write_text(result.stdout)
  if name=='old':
   assert result.returncode!=0 and 'ElementNotInteractableError' in result.stdout,result.stdout
  else:assert result.returncode==0,result.stdout
finally:
 page.write_text(corrected);spec.write_text(original_spec)
 server.terminate();server.wait(timeout=15)
print('Old cleanup reproduced ElementNotInteractableError; explicit waits passed.')
