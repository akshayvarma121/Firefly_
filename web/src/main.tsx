import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.tsx'
import { SolverProvider } from './context/SolverContext'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <SolverProvider>
      <App />
    </SolverProvider>
  </StrictMode>,
)
