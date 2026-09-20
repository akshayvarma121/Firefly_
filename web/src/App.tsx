import { RouterProvider, createBrowserRouter } from 'react-router-dom';
import { AppShell } from './components/AppShell';
import { Home } from './pages/Home';
import { Solve } from './pages/Solve';
import { Benchmarks } from './pages/Benchmarks';
import { Architecture } from './pages/Architecture';

const router = createBrowserRouter([
  {
    path: '/',
    element: <AppShell />,
    children: [
      { index: true, element: <Home /> },
      { path: 'solve', element: <Solve /> },
      { path: 'benchmarks', element: <Benchmarks /> },
      { path: 'architecture', element: <Architecture /> },
    ],
  },
]);

function App() {
  return <RouterProvider router={router} />;
}

export default App;
