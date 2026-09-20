import { useState, useEffect } from 'react';
import { NavLink, Outlet, useLocation } from 'react-router-dom';
import { motion, AnimatePresence } from 'framer-motion';
import { Menu, Home, Play, BarChart2, Server } from 'lucide-react';
import { SolverStatusLight } from './SolverStatus';
import { useSolverStore } from '../store/solverStore';
import { Toaster } from '@/components/ui/toaster';

// Pages that fill the viewport — no scroll, chart gets remaining height
const FILL_PAGES = ['/solve'];

export function AppShell() {
  const location = useLocation();
  const solverState = useSolverStore(s => s.state);

  // Auto-collapse at ≤1366px (projector / laptop)
  const [isSidebarOpen, setSidebarOpen] = useState(() => window.innerWidth > 1366);

  useEffect(() => {
    const handleResize = () => {
      if (window.innerWidth <= 1366) setSidebarOpen(false);
    };
    window.addEventListener('resize', handleResize);
    return () => window.removeEventListener('resize', handleResize);
  }, []);

  const isFillPage =
    FILL_PAGES.some(p => location.pathname.startsWith(p)) && solverState !== 'idle';

  const navItems = [
    { name: 'Home',         path: '/',              icon: Home },
    { name: 'Solve',        path: '/solve',         icon: Play },
    { name: 'Benchmarks',   path: '/benchmarks',    icon: BarChart2 },
    { name: 'Architecture', path: '/architecture',  icon: Server },
  ];

  return (
    <div className="flex h-screen w-full bg-background overflow-hidden text-text-primary font-sans">

      {/* ── Sidebar ─────────────────────────────────────────────── */}
      <aside
        className={`relative flex flex-col bg-panel border-r border-border transition-all duration-200 shrink-0 ${
          isSidebarOpen ? 'w-48' : 'w-12'
        }`}
      >
        {/* Collapse toggle */}
        <div className="flex items-center h-14 px-2 border-b border-border justify-between shrink-0">
          {isSidebarOpen && (
            <span className="font-mono text-[10px] tracking-widest text-text-muted pl-1 uppercase select-none">
              Menu
            </span>
          )}
          <button
            onClick={() => setSidebarOpen(o => !o)}
            title={isSidebarOpen ? 'Collapse sidebar' : 'Expand sidebar'}
            className="
              p-1.5 flex items-center justify-center w-8 h-8
              text-text-muted border border-transparent
              hover:text-text-primary hover:border-border
              active:opacity-75
              transition-colors duration-100
              focus-visible:outline-none focus-visible:ring-0
            "
            style={{ outline: 'none' }}
            onFocus={e => e.currentTarget.style.outline = '2px solid #E8A33D'}
            onBlur={e => e.currentTarget.style.outline = 'none'}
          >
            <Menu className="w-4 h-4" />
          </button>
        </div>

        {/* Nav items */}
        <nav className="flex-1 py-2 flex flex-col gap-0 overflow-y-auto">
          {navItems.map((item) => (
            <NavLink
              key={item.path}
              to={item.path}
              end={item.path === '/'}
              style={{ outline: 'none', position: 'relative' }}
              onFocus={e => e.currentTarget.style.outline = '2px solid #E8A33D'}
              onBlur={e => e.currentTarget.style.outline = 'none'}
              className={({ isActive }) =>
                [
                  'relative flex items-center gap-3 px-2 py-2 transition-colors duration-100',
                  'border-l-2',
                  isActive
                    ? 'border-signal text-signal bg-border'
                    : 'border-transparent text-text-muted hover:text-text-primary hover:bg-border hover:border-border',
                ].join(' ')
              }
              title={!isSidebarOpen ? item.name : undefined}
            >
              {({ isActive }) => (
                <>
                  {/* Framer Motion animated left-border indicator */}
                  {isActive && (
                    <motion.div
                      layoutId="sidebar-active-indicator"
                      className="absolute left-0 top-0 bottom-0 w-0.5 bg-signal"
                      transition={{ duration: 0.18, ease: 'easeOut' }}
                    />
                  )}
                  <item.icon className="w-4 h-4 shrink-0" />
                  {isSidebarOpen && (
                    <span className="text-sm font-sans whitespace-nowrap overflow-hidden text-ellipsis">
                      {item.name}
                    </span>
                  )}
                </>
              )}
            </NavLink>
          ))}
        </nav>
      </aside>

      {/* ── Main area ───────────────────────────────────────────── */}
      <div className="flex-1 flex flex-col min-w-0 overflow-hidden">

        {/* Topbar */}
        <header className="h-14 bg-panel border-b border-border flex items-center px-5 justify-between shrink-0">
          <div className="flex items-center gap-3 min-w-0">
            <h1 className="font-mono text-base tracking-widest text-text-primary uppercase whitespace-nowrap">
              Firefly
            </h1>
            <div className="flex items-center gap-2">
              <SolverStatusLight state={solverState} />
              <span className="text-[10px] font-mono text-text-muted uppercase tracking-widest hidden sm:inline">
                {solverState}
              </span>
            </div>
          </div>
          <span className="text-xs text-text-muted font-sans whitespace-nowrap hidden md:inline">
            by The Fireflies
          </span>
        </header>

        {/* Page content */}
        <main className="flex-1 overflow-hidden relative bg-background min-h-0">
          <AnimatePresence mode="wait">
            <motion.div
              key={location.pathname + '-' + isFillPage}
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              transition={{ duration: 0.12, ease: 'easeInOut' }}
              className={
                isFillPage
                  ? 'absolute inset-0 flex flex-col p-4 overflow-hidden'
                  : 'absolute inset-0 overflow-auto p-6'
              }
            >
              <Outlet />
            </motion.div>
          </AnimatePresence>
        </main>
      </div>

      <Toaster />
    </div>
  );
}
