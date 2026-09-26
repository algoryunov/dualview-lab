import { defineConfig, loadEnv } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, '..', 'DUALVIEW_')
  const target = env.DUALVIEW_PUBLIC_ORIGIN || 'https://localhost:8443'
  return {
    plugins: [react()],
    server: {
      // The native HTTPS endpoint is used for phone access; Vite is a local editor preview.
      host: '127.0.0.1',
      proxy: {
        '/api': {
          target,
          changeOrigin: true,
          secure: false,
          configure: (proxy) =>
            proxy.on('proxyReq', (request) => request.setHeader('Origin', target)),
        },
        '/ws': {
          target: target.replace(/^http/, 'ws'),
          ws: true,
          changeOrigin: true,
          secure: false,
          configure: (proxy) =>
            proxy.on('proxyReqWs', (request) => request.setHeader('Origin', target)),
        },
      },
    },
  }
})
