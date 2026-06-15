const path = require('path');

module.exports = async function beforePack() {
  const generatorPath = path.join(__dirname, 'generate-runtime-window.js');
  delete require.cache[require.resolve(generatorPath)];
  require(generatorPath);

  const { syncXcfgViewerResources } = require('./sync-xcfg-viewer-resources');
  syncXcfgViewerResources();

  const { prepareCli } = require('./prepare-cli');
  prepareCli({ strict: true });
};
