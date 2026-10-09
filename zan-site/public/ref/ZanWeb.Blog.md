# ZanWeb.Blog

> 源码: `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Services/BlogSeed.zan`


## BlogSeed (class)

- static void Register()

- static async bool Run(IFreeSql fsql)

- static async bool AddCategory(IFreeSql fsql, string name, string slug, int sortOrder)

- static Post PostOf(string title, int categoryId, string tags, int top, string summary, string body)
