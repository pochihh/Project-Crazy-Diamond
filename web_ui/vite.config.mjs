export default {
  server: {host:'127.0.0.1',strictPort:true,proxy:{
    '/api':process.env.DIAMOND_API || 'http://192.168.1.21:8000',
    '/ws':{target:process.env.DIAMOND_API || 'http://192.168.1.21:8000',ws:true},
  }},
};
