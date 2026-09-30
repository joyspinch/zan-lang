# ZanWeb.Dao.Blog

> 源码: `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/Blog/CategoryDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/Blog/CommentDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/Blog/PostDao.zan`


## CategoryDao (class)

- IDbConnection db;

- CategoryDao(AppController host)

- CategoryDao(IDbConnection db)

- async Category ById(int id)

- async Category BySlug(string slug)

- async List<Category> All()

- async List<Category> AllActive()

- async int Count()

- async int Insert(Category cat)

- async int Update(Category cat)

- async int Delete(int id)


## CommentDao (class)

- IDbConnection db;

- CommentDao(AppController host)

- CommentDao(IDbConnection db)

- async Comment ById(int id)

- async List<Comment> ByPost(int postId)

- async List<Comment> ByPostId(int postId)

- async int Count()

- async int CountPending()

- async int Insert(Comment c)

- async int SetStatus(int id, int status)

- async int Delete(int id)


## PostDao (class)

- IDbConnection db;

- static string ListKey="blog:list";

- static string PostKey(int id)

- PostDao(AppController host)

- PostDao(IDbConnection db)

- async List<Post> Recent(int take, string keyword)

- async Post ById(int id)

- async int Insert(Post post)

- async List<Post> AllRecent(int take, string keyword)

- async int CountAll()

- async int CountPublished()

- async int IncViews(int id, int views)

- async int SetPublished(int id, int published)

- async List<Post> RssLatest(int limit)

- async int Delete(int id)

- async int CountBy(List<OrmCond> conds)

- async List<Post> PagedListBy(List<OrmCond> conds, int skip, int limit)

- async int SetPublishedByIds(List<int> ids, int published)

- async int DeleteByIds(List<int> ids)
