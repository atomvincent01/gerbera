from pathlib import Path
import subprocess,os,time,urllib.request,json
root=Path.cwd();page=root/'test/e2e/page/home.page.js';routes=root/'mock-api/routes.js';spec=root/'test/e2e/client-load-proof.spec.js'
fixed=page.read_text();original_routes=routes.read_text();base=subprocess.check_output(['git','show','721497631f1ec880f18f8d8493a2f0784ebe593d:gerbera-web/test/e2e/page/home.page.js']).decode()
route="res.send(require(clientsMock.getResponse('')));";assert original_routes.count(route)==1
spec.write_text("""const { expect } = require('chai');
const TestUtils = require('./page/test-utils');
const HomePage = require('./page/home.page');
const LoginPage = require('./page/login.page');
describe('Client loading proof', () => {
  let driver, homePage, loginPage;
  before(async () => {
    driver = await TestUtils.newChromeDriver();
    await TestUtils.resetSuite('menu.spec.json', driver);
    homePage = new HomePage(driver);
    loginPage = new LoginPage(driver);
    await TestUtils.resetCookies(driver);
    await loginPage.get(TestUtils.home());
    await loginPage.password('pwd');
    await loginPage.username('user');
    await loginPage.submitLogin();
  });
  after(() => driver && driver.quit());
  it('observes rendered clients or rejects a failed load', async () => {
    await homePage.clickMenu('nav-clients');
    if (process.env.PROOF_MODE === 'failed') {
      let error;
      try { await homePage.clients(); } catch (e) { error = e; }
      expect(error && error.name).to.equal('TimeoutError');
    } else {
      const clients = await homePage.clients();
      expect(clients.length).to.equal(process.env.PROOF_MODE === 'empty' ? 0 : 5);
    }
  });
});
""")
env=dict(os.environ,npm_package_config_webserver_host='localhost',npm_package_config_webserver_port='3000',SELENIUM_BROWSER='chrome');results=[]
try:
 for name,helper,response,mode,expected in [
  ('old-delayed',base,"setTimeout(() => res.send(require(clientsMock.getResponse(''))), 1500);",'rows',False),
  ('fixed-delayed',fixed,"setTimeout(() => res.send(require(clientsMock.getResponse(''))), 1500);",'rows',True),
  ('fixed-normal',fixed,route,'rows',True),
  ('fixed-empty',fixed,"setTimeout(() => res.send({success: true, clients: [], groups: []}), 500);",'empty',True),
  ('fixed-failed',fixed,"res.status(503).send({success: false});",'failed',True)]:
  page.write_text(helper);routes.write_text(original_routes.replace(route,response));log=(root/('client-'+name+'.log')).open('w');server=subprocess.Popen(['node','index.js'],stdout=log,stderr=subprocess.STDOUT,env=env)
  try:
   ready=False
   for i in range(60):
    try:urllib.request.urlopen('http://localhost:3000/index.html',timeout=2).close();ready=True;break
    except OSError:time.sleep(.2)
   assert ready and server.poll() is None
   command=['node_modules/.bin/mocha','--config','test/.mocharc.yml',str(spec.relative_to(root)),'--retries','0']
   r=subprocess.run(command,env=dict(env,PROOF_MODE=mode),capture_output=True,text=True,timeout=90)
   with (root/('client-'+name+'.log')).open('a') as f:f.write(r.stdout+r.stderr)
   assert (r.returncode==0)==expected,(name,r.stdout,r.stderr)
   if not expected:assert 'expected +0 to equal 5' in r.stdout,r.stdout
   results.append({'case':name,'exit_code':r.returncode,'expected_success':expected,'matched_expectation':True});print(json.dumps(results[-1]),flush=True)
  finally:
   server.terminate();server.wait(timeout=15);log.close()
finally:
 page.write_text(fixed);routes.write_text(original_routes);spec.unlink(missing_ok=True)
(root/'client-proof.json').write_text(json.dumps(results,indent=2));print('Client rendering race reproduced; delayed, normal, empty and failed-response cases verified.',flush=True)
