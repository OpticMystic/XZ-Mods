import worker, { cleanup } from '../src/index';
export default {
  async fetch(req: Request, env: Parameters<typeof worker.fetch>[1], ctx: ExecutionContext) {
    // This wrapper is bundled only by local tests, never by wrangler.jsonc.
    if (new URL(req.url).pathname === '/__test/cleanup') {
      await cleanup(env,Number(req.headers.get('test-now')));
      return new Response('ok');
    }
    // Exercise Worker-side framing separately from undici's transport-level rejection.
    if (req.headers.has('test-declared-length')) {
      const headers=new Headers(req.headers);
      headers.set('Content-Length',req.headers.get('test-declared-length') ?? '');
      headers.delete('test-declared-length');
      req=new Request(req,{headers});
    }
    if(req.headers.get('test-storage-fault')==='r2-put') {
      env={...env,REPORTS:new Proxy(env.REPORTS,{
        get(target,key) {
          if(key==='put') return async () => { throw new Error('injected R2 outage'); };
          const value=Reflect.get(target,key); return typeof value==='function'?value.bind(target):value;
        }
      })};
    }
    if(req.headers.get('test-storage-fault')==='d1-accept') {
      env={...env,DB:new Proxy(env.DB,{
        get(target,key) {
          if(key==='withSession') return (...args: Parameters<typeof target.withSession>) => {
            const session=target.withSession(...args);
            return new Proxy(session,{
              get(db,key) {
                if(key==='prepare') return (sql: string) => {
                  const statement=db.prepare(sql);
                  if(!sql.includes("SET state='accepted'")) return statement;
                  return new Proxy(statement,{
                    get(stmt,key) {
                      if(key==='bind') return (...values: Parameters<typeof stmt.bind>) => {
                        const bound=stmt.bind(...values);
                        return new Proxy(bound,{
                          get(item,key) {
                            if(key==='run') return async () => { throw new Error('injected acceptance failure'); };
                            const value=Reflect.get(item,key); return typeof value==='function'?value.bind(item):value;
                          }
                        });
                      };
                      const value=Reflect.get(stmt,key); return typeof value==='function'?value.bind(stmt):value;
                    }
                  });
                };
                const value=Reflect.get(db,key); return typeof value==='function'?value.bind(db):value;
              }
            });
          };
          const value=Reflect.get(target,key); return typeof value==='function'?value.bind(target):value;
        }
      })};
    }
    return worker.fetch(req,env,ctx);
  }
};
