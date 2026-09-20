import asyncio
from playwright.async_api import async_playwright

async def run():
    async with async_playwright() as p:
        browser = await p.chromium.launch(headless=True)
        page = await browser.new_page()

        print('=== BROWSER CONSOLE ===')
        page.on('console', lambda msg: print(f'CONSOLE [{msg.type}]: {msg.text}'))
        page.on('pageerror', lambda err: print(f'PAGE ERROR: {err}'))

        print('=== NETWORK LOGS ===')
        page.on('request', lambda req: print(f'REQ: {req.method} {req.url}'))
        page.on('response', lambda res: print(f'RES: {res.status} {res.url}'))

        def on_web_socket(ws):
            print(f'WS OPEN: {ws.url}')
            ws.on('framereceived', lambda frame: print(f'WS RECV: {frame}'))
            ws.on('framesent', lambda frame: print(f'WS SENT: {frame}'))
            ws.on('close', lambda: print(f'WS CLOSED: {ws.url}'))
            ws.on('socketerror', lambda err: print(f'WS ERROR: {err}'))

        page.on('websocket', on_web_socket)

        await page.goto('http://localhost:5173/solve')
        
        # Select test problem 2
        await page.select_option('select', 'test_problem2.mps')
        
        await asyncio.sleep(1) # wait for fetch of mps
        
        # Click the solve button
        # There's a button with text 'New Solve' maybe? Wait, no, the button might not exist initially.
        # Oh, let's see what the solve button text is! I haven't checked how it is initiated.
        # Wait, the Solve.tsx code: I didn't see the 'Execute' button. Let's look at the UI.
        
        # We can just click text 'Solve' or we can evaluate JS to trigger it.
        # Let's write the rest of the UI code.
